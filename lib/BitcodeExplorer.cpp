/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <magnifier/BitcodeExplorer.h>

#include <magnifier/IFunctionResolver.h>
#include <magnifier/ISubstitutionObserver.h>
#include "IdCommentWriter.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Analysis/ConstantFolding.h>
#include <llvm/Passes/PassBuilder.h>

#include <iostream>

#if 1
#  define MAG_DEBUG(...) __VA_ARGS__
#else
#  define MAG_DEBUG(...)
#endif

namespace magnifier {
namespace {

[[nodiscard]] static std::string GetSubstituteHookName(llvm::Type *type) {
    return llvm::formatv("substitute_hook_{0}", reinterpret_cast<uintptr_t>(type)).str();
}

// Try to verify a module.
static bool VerifyModule(llvm::Module *module, llvm::Function *function) {
    std::string error;
    llvm::raw_string_ostream error_stream(error);
    if (llvm::verifyModule(*module, &error_stream)) {
        function->print(error_stream);
        error_stream.flush();
        std::cerr << "Error verifying module: " << error;
        assert(false);
        return false;
    } else {
        return true;
    }
}
}

BitcodeExplorer::BitcodeExplorer(llvm::LLVMContext &llvm_context)
    : llvm_context(llvm_context),
      md_explorer_id(llvm_context.getMDKindID("explorer.id")),
      md_explorer_source_id(llvm_context.getMDKindID("explorer.source_id")),
      md_explorer_block_id(llvm_context.getMDKindID("explorer.block_id")),
      md_explorer_substitution_kind_id(llvm_context.getMDKindID("explorer.substitution_kind_id")),
      annotator(std::make_unique<IdCommentWriter>(*this)),
      function_map(),
      instruction_map(),
      block_map(),
      argument_map(),
      value_id_counter(1),
      hook_functions() {}

void BitcodeExplorer::TakeModule(std::unique_ptr<llvm::Module> module) {
    llvm::LLVMContext &module_context = module->getContext();
    assert(std::addressof(module_context) == std::addressof(llvm_context));

    for (auto &function : module->functions()) {
        if (function.isDeclaration() || function.isIntrinsic()) {
            continue;
        }
        UpdateMetadata(function);
    }
    opened_modules.push_back(std::move(module));
}

void BitcodeExplorer::ForEachFunction(const std::function<void(ValueId, llvm::Function & , FunctionKind)> &callback) {
    for (const auto &[function_id, weak_vh] : function_map) {
        if (llvm::Function *function = cast_or_null<llvm::Function>(weak_vh)) {
            FunctionKind kind = GetId(*function, ValueIdKind::kOriginal) == GetId(*function, ValueIdKind::kDerived) ? FunctionKind::kOriginal : FunctionKind::kGenerated;
            callback(function_id, *function, kind);
        }
    }
}

bool BitcodeExplorer::PrintFunction(ValueId function_id, llvm::raw_ostream &output_stream) {
    auto function_pair = function_map.find(function_id);
    if (function_pair == function_map.end()) {
        return false;
    }

    llvm::Function *function = cast_or_null<llvm::Function>(function_pair->second);
    if (!function) {
        return false;
    }

    function->print(output_stream, annotator.get());
    return true;
}

Result<ValueId, InlineError> BitcodeExplorer::InlineFunctionCall(ValueId instruction_id, IFunctionResolver &resolver, ISubstitutionObserver &substitution_observer) {
    auto instruction_pair = instruction_map.find(instruction_id);
    if (instruction_pair == instruction_map.end()) {
        return InlineError::kInstructionNotFound;
    }

    llvm::Instruction *instruction = cast_or_null<llvm::Instruction>(instruction_pair->second);
    if (!instruction) {
        return InlineError::kInstructionNotFound;
    }

    auto call_base = llvm::dyn_cast<llvm::CallBase>(instruction);
    if (!call_base) {
        return InlineError::kNotACallBaseInstruction;
    }

    llvm::Function *called_function = call_base->getCalledFunction();
    llvm::Module *func_module = called_function->getParent();

    // try to resolve declarations
    llvm::FunctionType *original_callee_type = called_function->getFunctionType();
    called_function = resolver.ResolveCallSite(call_base, called_function);
    if (!called_function) {
        return InlineError::kCannotResolveFunction;
    } else if (called_function->isDeclaration()) {
        return InlineError::kCannotResolveFunction;
    } else if (called_function->isVarArg()) {
        return InlineError::kVariadicFunction;
    } else if (called_function->getFunctionType() != original_callee_type) {
        return InlineError::kResolveFunctionTypeMismatch;
    }

    // Need to index the newly resolved function if it's the first time encountering it
    if (GetId(*called_function, ValueIdKind::kDerived) == kInvalidValueId) {
        UpdateMetadata(*called_function);
    }

    llvm::Function *caller_function = call_base->getFunction();
    assert(caller_function != nullptr);

    // clone and modify the called function prior to inlining
    llvm::ValueToValueMapTy called_value_map;
    llvm::Function *cloned_called_function = llvm::CloneFunction(called_function, called_value_map);

    // Add hook for each argument

    // As an example, given function:
    //
    // foo(x, y) {
    //   ...
    //   z = x + y
    //   ...
    // }
    //
    // We will append two calls to the substitute hook at the start of the function
    // First, argument `x` will be hooked. After the first loop iteration, the function becomes:
    //
    // foo(x, y) {
    //   temp_val = substitute_hook(x, x)
    //   ...
    //   z = temp_val + y
    //   ...
    // }
    //
    // The `substitute_hook` takes two parameters: the old value and the new value.
    // It's more useful in the case of value substitution. Here we just use the same value `x` for both.
    // Then, the same process is applied again for `y`:
    //
    // foo(x, y) {
    //   temp_val = substitute_hook(x, x)
    //   temp_val2 = substitute_hook(y, y)
    //   ...
    //   z = temp_val + temp_val2
    //   ...
    // }

    // This hooking process helps us observe and control the substitution of arguments
    // during the inlining process. Now, when inlining the function `foo`:
    //
    // bar() {
    //   ...
    //   foo(1,2)
    //   ...
    // }
    //
    // There's a new intermediate stage:
    //
    // bar() {
    //   ...
    //   temp_val = substitute_hook(1, 1)
    //   temp_val2 = substitute_hook(2, 2)
    //   ...
    //   z = temp_val + temp_val2
    //   ...
    // }
    //
    // Before the final result is obtained by calling `ElideSubstitutionHooks`:
    //
    // bar() {
    //   ...
    //   z = 1 + 2
    //   ...
    // }

    llvm::BasicBlock &entry = cloned_called_function->getEntryBlock();
    llvm::IRBuilder<> builder(&entry.front());
    for (llvm::Argument &arg : cloned_called_function->args()) {
        llvm::CallInst *call_inst = CreateHookCallInst(arg.getType(), func_module, SubstitutionKind::kArgument, &arg, &arg);
        builder.Insert(call_inst);

        arg.replaceUsesWithIf(call_inst, [call_inst](const llvm::Use &use) -> bool {
            return use.getUser() != call_inst;
        });
    }

    // clone and modify the caller function
    llvm::ValueToValueMapTy caller_value_map;
    llvm::Function *cloned_caller_function = llvm::CloneFunction(caller_function, caller_value_map);

    llvm::CallBase *cloned_call_base = nullptr;
    for (auto &cloned_instruction : llvm::instructions(cloned_caller_function)) {
        if (this->GetId(cloned_instruction, ValueIdKind::kDerived) == instruction_id) {
            cloned_call_base = llvm::dyn_cast<llvm::CallBase>(&cloned_instruction);
        }
    }
    assert(cloned_call_base != nullptr);

    // hook the function call if the return type is not void

    // As an example, given functions:
    //
    // foo(x, y) {
    //   return 10
    // }
    //
    // bar() {
    //   ...
    //   a = foo(1,2)
    //   ...
    //   b = a + 1
    //   ...
    // }
    //
    // We are going to transform `bar` to become:
    //
    // bar() {
    //   ...
    //   temp_val = foo(1,2)
    //   a = substitute_hook(temp_val, temp_val)
    //   ...
    //   b = a + 1
    //   ...
    // }
    //
    // The `substitute_hook` takes two parameters: the old value and the new value.
    // It's more useful in the case of value substitution. Here we just use the same value `temp_val` for both.
    // With this transformation in place, inlining the function will result in:
    //
    // bar() {
    //   ...
    //   a = substitute_hook(10, 10)
    //   ...
    //   b = a + 1
    //   ...
    // }
    //
    // And `ElideSubstitutionHooks` will explicitly substitute in the return value:
    //
    // bar() {
    //   ...
    //   b = 10 + 1
    //   ...
    // }
    //
    // This offers us better insight into the substitution of the return value.

    if (!cloned_call_base->getType()->isVoidTy()) {
        auto *dup_call_base = dyn_cast<llvm::CallBase>(cloned_call_base->clone());
        dup_call_base->setName("temp_val");
        dup_call_base->setCalledFunction(cloned_called_function);
        dup_call_base->insertBefore(cloned_call_base);

        std::string original_name = cloned_call_base->getName().str();
        cloned_call_base->setName("to_delete");

        llvm::CallInst *substituted_call = CreateHookCallInst(cloned_call_base->getType(), func_module, SubstitutionKind::kReturnValue, dup_call_base, dup_call_base);
        substituted_call->insertBefore(cloned_call_base);
        substituted_call->setName(original_name);

        cloned_call_base->replaceAllUsesWith(substituted_call);
        cloned_call_base->eraseFromParent();

        cloned_call_base = dup_call_base;
    }

    MAG_DEBUG(VerifyModule(func_module, cloned_caller_function));

    // Do the inlining
    llvm::InlineFunctionInfo info;
    llvm::InlineResult inline_result = llvm::InlineFunction(*cloned_call_base, info);
    if (!inline_result.isSuccess()) {
        // clean up resources
        cloned_called_function->eraseFromParent();
        cloned_caller_function->eraseFromParent();

        return InlineError::kInlineOperationFailed;
    }

    // delete the cloned copy of the called_function after inlining it
    cloned_called_function->eraseFromParent();

    MAG_DEBUG(VerifyModule(func_module, cloned_caller_function));

    // elide the hooks
    ElideSubstitutionHooks(*cloned_caller_function, substitution_observer);

    // update all metadata
    UpdateMetadata(*cloned_caller_function);

    MAG_DEBUG(VerifyModule(func_module, cloned_caller_function));

    return GetId(*cloned_caller_function, ValueIdKind::kDerived);

}

void BitcodeExplorer::UpdateMetadata(llvm::Function &function) {
    ValueId function_id = value_id_counter++;
    SetId(function, function_id, ValueIdKind::kDerived);
    if (GetId(function, ValueIdKind::kOriginal) == kInvalidValueId) {
        SetId(function, function_id, ValueIdKind::kOriginal);
    }
    function_map.emplace(function_id, &function);
    
    // Assign ids to function arguments. The assertion should always hold.
    for (llvm::Argument &argument : function.args()) {
        ValueId argument_id = value_id_counter++;
        argument_map.emplace(argument_id, &argument);
        assert(argument_id == (function_id + argument.getArgNo() + 1));
    }


    for (auto &instruction : llvm::instructions(function)) {
        ValueId new_instruction_id = value_id_counter++;
        SetId(instruction, new_instruction_id, ValueIdKind::kDerived);

        // for an instruction without a source, set itself to be the self
        if (GetId(instruction, ValueIdKind::kOriginal) == kInvalidValueId) {
            SetId(instruction, new_instruction_id, ValueIdKind::kOriginal);
        }

        if (GetId(instruction, ValueIdKind::kBlock) != kInvalidValueId) {
            RemoveId(instruction, ValueIdKind::kBlock);
        }

        instruction_map.emplace(new_instruction_id, &instruction);
    }

    for (llvm::BasicBlock &block : function) {
        ValueId new_block_id = value_id_counter++;
        llvm::Instruction *terminator_instr = block.getTerminator();
        if (!terminator_instr) {
            continue;
        }
        SetId(*terminator_instr, new_block_id, ValueIdKind::kBlock);

        block_map.emplace(new_block_id, &block);
    }
}

void BitcodeExplorer::ElideSubstitutionHooks(llvm::Function &function, ISubstitutionObserver &substitution_observer) {
    // Get a const reference to the module data layout later used for constant folding
    llvm::Module *func_module = function.getParent();
    const llvm::DataLayout &module_data_layout = func_module->getDataLayout();

    // The tuple contains:
    // 1. The instruction we are substituting
    //   This is usually in the form of `%abc = call i32 @substitute_hook_4949385960(i32 %old_val, i32 %new_val)`.
    //   But in the case of constant folding, it can also be an instruction with only constant operands similar
    //   to `%add.i = add nsw i32 31, 30`.
    // 2. The old value before the substitution
    // 3. The new value after the substitution
    std::vector<std::tuple<llvm::Instruction *, llvm::Value *, llvm::Value *>> subs;

    // Find all uses of the hook functions
    for (auto [type, function_callee_obj] : hook_functions) {
        auto *hook_func = dyn_cast<llvm::Function>(function_callee_obj.getCallee());
        if (!hook_func) {
            continue;
        }

        for (llvm::Use &use: hook_func->uses()) {
            // only enqueue hook calls inside the given function
            auto *call_base = dyn_cast<llvm::CallBase>(use.getUser());
            if (call_base && call_base->getFunction() == &function) {
                subs.emplace_back(call_base, call_base->getArgOperand(0), call_base->getArgOperand(1));
            }
        }
    }

    // substitute the values
    while (!subs.empty()) {
        auto [inst, old_val, new_val] = subs.back();
        subs.pop_back();

        ValueId substitution_id = GetId(*inst, ValueIdKind::kSubstitution);
        assert(substitution_id != kInvalidValueId);

        auto substitution_kind = static_cast<SubstitutionKind>(substitution_id);
        llvm::Value *updated_sub_val = substitution_observer.PerformSubstitution(inst, old_val, new_val,substitution_kind);

        // Assume equivalence for value substitution
        if (substitution_kind == SubstitutionKind::kValueSubstitution && updated_sub_val != old_val) {
            llvm::IRBuilder builder(inst);
            builder.CreateAssumption(builder.CreateCmp(llvm::CmpInst::ICMP_EQ, old_val, updated_sub_val));
        }

        // Check we are not replacing the value with itself
        if (updated_sub_val != inst) {
            // Iterate through and replace each occurrence of the value.
            // Attempt constant folding and add additional substitution hooks to the `subs` list.
            while (!inst->use_empty()) {
                llvm::Use &substitute_location = *inst->use_begin();
                substitute_location.set(updated_sub_val);

                auto *target_instr = cast<llvm::Instruction>(substitute_location.getUser());
                llvm::Constant *fold_result = llvm::ConstantFoldInstruction(target_instr, module_data_layout);
                if (fold_result) {
                    SetId(*target_instr,static_cast<std::underlying_type_t<SubstitutionKind>>(SubstitutionKind::kConstantFolding),ValueIdKind::kSubstitution);
                    subs.emplace_back(target_instr, target_instr, fold_result);
                }
            }

            // Remove the substitution hook
            inst->eraseFromParent();
        }

        // Remove `old_val` if it's an instruction and no longer in use.
        // Have to make sure it's not the same as `inst`.
        if (old_val != inst) {
            auto old_instr = dyn_cast<llvm::Instruction>(old_val);
            if (old_instr && old_instr->getParent() && old_instr->use_empty()) {
                old_instr->eraseFromParent();
            }
        }
    }

    // remove all the hook functions after eliding them
    for (auto [type, function_callee_obj] : hook_functions) {
        if (auto *hook_func = dyn_cast<llvm::Function>(function_callee_obj.getCallee())) {
            assert(hook_func->use_empty());
            hook_func->eraseFromParent();
        }
    }

    // clear the hook function map after each high-level operation
    hook_functions.clear();
}

// Convert from opaque type `ValueIdKind` to actual llvm `kind_id`.
unsigned BitcodeExplorer::ValueIdKindToKindId(ValueIdKind kind) const {
    switch (kind) {
        case ValueIdKind::kOriginal:
            return md_explorer_source_id;
        case ValueIdKind::kDerived:
            return md_explorer_id;
        case ValueIdKind::kBlock:
            return md_explorer_block_id;
        case ValueIdKind::kSubstitution:
            return md_explorer_substitution_kind_id;
    }
    assert(false);
    return 0;  // An invalid metadata id.
}

// Returns the value ID for `function`, or `kInvalidValueId` if no ID is found.
ValueId BitcodeExplorer::GetId(const llvm::Function &function, ValueIdKind kind) const {
    llvm::MDNode* mdnode = function.getMetadata(ValueIdKindToKindId(kind));
    if (mdnode == nullptr) { return kInvalidValueId; }
    return cast<llvm::ConstantInt>(cast<llvm::ConstantAsMetadata>(mdnode->getOperand(0))->getValue())->getZExtValue();
}

// Returns the value ID for `instruction`, or `kInvalidValueId` if no ID is found.
ValueId BitcodeExplorer::GetId(const llvm::Instruction &instruction, ValueIdKind kind) const {
    llvm::MDNode* mdnode = instruction.getMetadata(ValueIdKindToKindId(kind));
    if (mdnode == nullptr) { return kInvalidValueId; }
    return cast<llvm::ConstantInt>(cast<llvm::ConstantAsMetadata>(mdnode->getOperand(0))->getValue())->getZExtValue();
}

// Set an id inside the metadata of a function.
void BitcodeExplorer::SetId(llvm::Function &function, ValueId value, ValueIdKind kind) const {
    llvm::MDNode *mdnode = llvm::MDNode::get(llvm_context,
                                             llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                                                     llvm_context,
                                                     llvm::APInt(64, value, false))));
    function.setMetadata(ValueIdKindToKindId(kind), mdnode);
}

// Set an id inside the metadata of an instruction.
void BitcodeExplorer::SetId(llvm::Instruction &instruction, ValueId value, ValueIdKind kind) const {
    llvm::MDNode *mdnode = llvm::MDNode::get(llvm_context,
                                             llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                                                     llvm_context,
                                                     llvm::APInt(64, value, false))));
    instruction.setMetadata(ValueIdKindToKindId(kind), mdnode);
}

// Remove id of `kind` from `function` metadata.
void BitcodeExplorer::RemoveId(llvm::Function &function, ValueIdKind kind) {
    function.setMetadata(ValueIdKindToKindId(kind), nullptr);
}

// Remove id of `kind` from `instruction` metadata.
void BitcodeExplorer::RemoveId(llvm::Instruction &instruction, ValueIdKind kind) {
    instruction.setMetadata(ValueIdKindToKindId(kind), nullptr);
}

// Get a `FunctionCallee` object for the given `type`. Create the function in `func_module` if it doesn't exist.
// In addition, the object is added to the `hook_functions` map.
llvm::FunctionCallee BitcodeExplorer::GetHookFunction(llvm::Type *type, llvm::Module *func_module) {
    llvm::FunctionCallee &hook_func = hook_functions[type];
    if (hook_func) {
        return hook_functions[type];
    }

    llvm::FunctionType *func_type = llvm::FunctionType::get(
            type, {type, type},
            false);
    hook_func = func_module->getOrInsertFunction(GetSubstituteHookName(type), func_type);
    return hook_func;
}

llvm::CallInst *BitcodeExplorer::CreateHookCallInst(llvm::Type *type, llvm::Module *func_module, SubstitutionKind hook_kind, llvm::Value *old_val, llvm::Value *new_val) {
    llvm::CallInst *call_instr = llvm::CallInst::Create(GetHookFunction(type, func_module), {old_val, new_val}, "temp_val");
    SetId(*call_instr, static_cast<std::underlying_type_t<SubstitutionKind>>(hook_kind), ValueIdKind::kSubstitution);
    return call_instr;
}

Result <ValueId, SubstitutionError> BitcodeExplorer::SubstituteInstructionWithValue(ValueId instruction_id, uint64_t value, ISubstitutionObserver &observer) {
    auto instruction_pair = instruction_map.find(instruction_id);
    if (instruction_pair == instruction_map.end()) {
        return SubstitutionError::kIdNotFound;
    }

    llvm::Instruction *instruction = cast_or_null<llvm::Instruction>(instruction_pair->second);
    if (!instruction) {
        return SubstitutionError::kIdNotFound;
    }
    if (!instruction->getType()->isIntegerTy()) {
        return SubstitutionError::kIncorrectType;
    }

    llvm::Function *function = instruction->getFunction();
    assert(function != nullptr);
    llvm::Module *func_module = function->getParent();

    // clone and modify the function
    llvm::ValueToValueMapTy value_map;
    llvm::Function *cloned_function = llvm::CloneFunction(function, value_map);

    llvm::Instruction *cloned_instr = nullptr;
    for (auto &cloned_instruction: llvm::instructions(cloned_function)) {
        if (GetId(cloned_instruction, ValueIdKind::kDerived) == instruction_id) {
            cloned_instr = &cloned_instruction;
        }
    }
    assert(cloned_instr != nullptr);

    // Here we want to add in a substitution hook to facilitate the substitution process.
    // The real value replacement really happens inside `ElideSubstitutionHooks`.
    // For example, given a function:
    //
    // foo(x, y) {
    //   ...
    //   a = x + y
    //   b = a + 1
    //   ...
    // }
    //
    // And we want to replace `a` with `10`. Then the function should be transformed into:
    //
    // foo(x, y) {
    //   ...
    //   temp_val = x + y
    //   a = substitute_hook(temp_val, 10)
    //   b = a + 1
    //   ...
    // }
    //
    // The first parameter being the old value and the second one being the new value.

    llvm::ConstantInt *const_val = llvm::ConstantInt::get(llvm_context, llvm::APInt(cloned_instr->getType()->getIntegerBitWidth(), value, false));
    llvm::CallInst *substituted_call = CreateHookCallInst(cloned_instr->getType(), func_module, SubstitutionKind::kValueSubstitution, cloned_instr, const_val);
    substituted_call->insertAfter(cloned_instr);

    cloned_instr->replaceUsesWithIf(substituted_call, [substituted_call](const llvm::Use &use) -> bool {
        return use.getUser() != substituted_call;
    });

    MAG_DEBUG(VerifyModule(func_module, cloned_function));

    ElideSubstitutionHooks(*cloned_function, observer);

    UpdateMetadata(*cloned_function);

    MAG_DEBUG(VerifyModule(func_module, cloned_function));

    return GetId(*cloned_function, ValueIdKind::kDerived);
}

Result<ValueId, SubstitutionError> BitcodeExplorer::SubstituteArgumentWithValue(ValueId argument_id, uint64_t value, ISubstitutionObserver &observer) {
    auto argument_pair = argument_map.find(argument_id);
    if (argument_pair == argument_map.end()) {
        if (function_map.find(argument_id) != function_map.end()) {
            return SubstitutionError::kCannotUseFunctionId;
        }
        return SubstitutionError::kIdNotFound;
    }

    llvm::Argument *argument = cast_or_null<llvm::Argument>(argument_pair->second);
    if (!argument) {
        return SubstitutionError::kIdNotFound;
    }
    if (!argument->getType()->isIntegerTy()) {
        return SubstitutionError::kIncorrectType;
    }

    llvm::Function *function = argument->getParent();
    assert(function != nullptr);
    llvm::Module *func_module = function->getParent();

    // clone and modify the function
    llvm::ValueToValueMapTy value_map;
    llvm::Function *cloned_function = llvm::CloneFunction(function, value_map);

    llvm::Argument *cloned_argument = cloned_function->getArg(argument->getArgNo());


    // Here we want to add in a substitution hook to facilitate the substitution process.
    // The real value replacement really happens inside `ElideSubstitutionHooks`.
    // For example, given a function:
    //
    // foo(x, y) {
    //   ...
    //   a = x + y
    //   ...
    // }
    //
    // And we want to replace `y` with `10`. Then the function should be transformed into:
    //
    // foo(x, y) {
    //   temp_val = substitute_hook(y, 10)
    //   ...
    //   a = x + temp_val
    //   ...
    // }
    //
    // The first parameter being the old value (`y`) and the second one being the new value (`10`).

    llvm::ConstantInt *const_val = llvm::ConstantInt::get(llvm_context, llvm::APInt(cloned_argument->getType()->getIntegerBitWidth(), value, false));
    llvm::CallInst *substitute_hook_call = CreateHookCallInst(cloned_argument->getType(), func_module, SubstitutionKind::kValueSubstitution, cloned_argument, const_val);
    substitute_hook_call->insertBefore(&cloned_function->getEntryBlock().front());

    cloned_argument->replaceUsesWithIf(substitute_hook_call, [substitute_hook_call](const llvm::Use &use) -> bool {
        return use.getUser() != substitute_hook_call;
    });

    MAG_DEBUG(VerifyModule(func_module, cloned_function));

    ElideSubstitutionHooks(*cloned_function, observer);

    UpdateMetadata(*cloned_function);

    MAG_DEBUG(VerifyModule(func_module, cloned_function));

    return GetId(*cloned_function, ValueIdKind::kDerived);
}

Result<ValueId, OptimizationError> BitcodeExplorer::OptimizeFunction(ValueId function_id, const llvm::PassBuilder::OptimizationLevel &optimization_level) {
    if (optimization_level == llvm::PassBuilder::OptimizationLevel::O0) {
        return OptimizationError::kInvalidOptimizationLevel;
    }

    auto function_pair = function_map.find(function_id);
    if (function_pair == function_map.end()) {
        return OptimizationError::kIdNotFound;
    }

    llvm::Function *function = cast_or_null<llvm::Function>(function_pair->second);
    if (!function) {
        return OptimizationError::kIdNotFound;
    }

    llvm::Module *func_module = function->getParent();

    // Clone the function
    llvm::ValueToValueMapTy value_map;
    llvm::Function *cloned_function = llvm::CloneFunction(function, value_map);

    // Create the analysis managers
    llvm::LoopAnalysisManager loop_analysis_manager;
    llvm::FunctionAnalysisManager function_analysis_manager;
    llvm::CGSCCAnalysisManager cgscc_analysis_manager;
    llvm::ModuleAnalysisManager module_analysis_manager;

    // Create the new pass manager builder
    llvm::PassBuilder pass_builder;

    function_analysis_manager.registerPass([&] { return pass_builder.buildDefaultAAPipeline(); });

    pass_builder.registerModuleAnalyses(module_analysis_manager);
    pass_builder.registerCGSCCAnalyses(cgscc_analysis_manager);
    pass_builder.registerFunctionAnalyses(function_analysis_manager);
    pass_builder.registerLoopAnalyses(loop_analysis_manager);
    pass_builder.crossRegisterProxies(loop_analysis_manager, function_analysis_manager, cgscc_analysis_manager, module_analysis_manager);

    llvm::FunctionPassManager function_pass_manager = pass_builder.buildFunctionSimplificationPipeline(optimization_level, llvm::PassBuilder::ThinLTOPhase::None);

    function_pass_manager.run(*cloned_function, function_analysis_manager);

    UpdateMetadata(*cloned_function);

    MAG_DEBUG(VerifyModule(func_module, cloned_function));

    return GetId(*cloned_function, ValueIdKind::kDerived);
}

std::optional<DeletionError> BitcodeExplorer::DeleteFunction(ValueId function_id) {
    auto function_pair = function_map.find(function_id);
    if (function_pair == function_map.end()) {
        return DeletionError::kIdNotFound;
    }

    llvm::Function *function = cast_or_null<llvm::Function>(function_pair->second);
    if (!function) {
        return DeletionError::kIdNotFound;
    }

    // Check if the function is referenced by another function
    bool function_used_by_other_function = false;
    for (llvm::Use &use : function->uses()) {
       auto *instr = dyn_cast<llvm::Instruction>(use.getUser());
       if (instr && instr->getFunction() != function) {
           function_used_by_other_function = true;
           break;
       }
    }

    if (function_used_by_other_function) {
        return DeletionError::kFunctionInUse;
    }

    // Remove function from the various maps
    function_map.erase(function_id);

    for (llvm::Argument &argument : function->args()) {
        argument_map.erase(function_id+argument.getArgNo()+1);
    }

    for (auto &instruction : llvm::instructions(function)) {
        instruction_map.erase(GetId(instruction, ValueIdKind::kDerived));
    }

    for (llvm::BasicBlock &block : *function) {
        llvm::Instruction *terminator_instr = block.getTerminator();
        if (!terminator_instr) {
            continue;
        }
        block_map.erase(GetId(*terminator_instr, ValueIdKind::kBlock));
    }

    // Delete the function
    function->eraseFromParent();

    return std::nullopt;
}

BitcodeExplorer::~BitcodeExplorer() = default;
}