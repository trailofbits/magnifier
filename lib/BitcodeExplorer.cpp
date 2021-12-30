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
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Transforms/Utils/Cloning.h>

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
static bool VerifyModule(llvm::Module *module) {
    std::string error;
    llvm::raw_string_ostream error_stream(error);
    if (llvm::verifyModule(*module, &error_stream)) {
        error_stream.flush();
        std::cerr << "Error verifying module: " << error;
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
    //   temp_val = substitute_hook(x)
    //   ...
    //   z = temp_val + y
    //   ...
    // }
    //
    // Then, the same process is applied again for `y`:
    //
    // foo(x, y) {
    //   temp_val = substitute_hook(x)
    //   temp_val2 = substitute_hook(y)
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
    //   temp_val = substitute_hook(1)
    //   temp_val2 = substitute_hook(2)
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
    llvm::IRBuilder<> builder(&entry);
    builder.SetInsertPoint(&entry, entry.begin());
    for (llvm::Argument &arg : cloned_called_function->args()) {
        llvm::CallInst *call_inst = builder.CreateCall(GetHookFunction(arg.getType(), func_module), {&arg}, "temp_val");
        SetId(*call_inst, static_cast<std::underlying_type_t<SubstitutionKind>>(SubstitutionKind::kArgument), ValueIdKind::kSubstitution);

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
    //   a = substitute_hook(temp_val)
    //   ...
    //   b = a + 1
    //   ...
    // }
    //
    // With this transformation in place, inlining the function will result in:
    //
    // bar() {
    //   ...
    //   a = substitute_hook(10)
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

        llvm::CallInst *substituted_call = llvm::CallInst::Create(GetHookFunction(cloned_call_base->getType(), func_module), { dup_call_base }, original_name, cloned_call_base);
        SetId(*substituted_call, static_cast<std::underlying_type_t<SubstitutionKind>>(SubstitutionKind::kReturnValue), ValueIdKind::kSubstitution);

        cloned_call_base->replaceAllUsesWith(substituted_call);
        cloned_call_base->eraseFromParent();

        cloned_call_base = dup_call_base;
    }

    MAG_DEBUG(VerifyModule(func_module));

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

    MAG_DEBUG(VerifyModule(func_module));

    // elide the hooks
    ElideSubstitutionHooks(*cloned_caller_function, substitution_observer);

    // update all metadata
    UpdateMetadata(*cloned_caller_function);

    MAG_DEBUG(VerifyModule(func_module));

    return GetId(*cloned_caller_function, ValueIdKind::kDerived);

}

void BitcodeExplorer::UpdateMetadata(llvm::Function &function) {
    ValueId function_id = value_id_counter++;
    SetId(function, function_id, ValueIdKind::kDerived);
    if (GetId(function, ValueIdKind::kOriginal) == kInvalidValueId) {
        SetId(function, function_id, ValueIdKind::kOriginal);
    }
    function_map.emplace(function_id, &function);


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
    // find all uses of the hook functions
    std::vector<std::pair<llvm::Use *, llvm::Value *>> subs;
    for (auto [type, function_callee_obj] : hook_functions) {
        if (auto *hook_func = dyn_cast<llvm::Function>(function_callee_obj.getCallee())) {
            for (llvm::Use &use : hook_func->uses()) {
                subs.emplace_back(&use, use.getUser()->getOperand(0));
            }
        }
    }

    // substitute the values
    for (auto [use, sub_val] : subs) {
        llvm::User *user = use->getUser();
        if (auto *inst = dyn_cast<llvm::Instruction>(user)) {
            ValueId substitution_id = GetId(*inst, ValueIdKind::kSubstitution);
            assert(substitution_id != kInvalidValueId);

            inst->replaceAllUsesWith(substitution_observer.PerformSubstitution(use, sub_val, static_cast<SubstitutionKind>(substitution_id)));
            inst->eraseFromParent();
        }
    }

    // remove all the hook functions after eliding them
    for (auto [type, function_callee_obj] : hook_functions) {
        if (auto *hook_func = dyn_cast<llvm::Function>(function_callee_obj.getCallee())) {
            assert(hook_func->uses().empty());
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
            type, {type},
            false);
    hook_func = func_module->getOrInsertFunction(GetSubstituteHookName(type), func_type);
    return hook_func;
}

BitcodeExplorer::~BitcodeExplorer() = default;
}