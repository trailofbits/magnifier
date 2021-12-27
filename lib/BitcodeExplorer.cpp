/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <magnifier/BitcodeExplorer.h>

#include "IdCommentWriter.h"
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Transforms/Utils/Cloning.h>

namespace magnifier {
BitcodeExplorer::BitcodeExplorer(llvm::LLVMContext &llvm_context)
    : llvm_context(llvm_context),
      md_explorer_id(llvm_context.getMDKindID("explorer.id")),
      md_explorer_source_id(llvm_context.getMDKindID("explorer.source_id")),
      annotator(std::make_unique<IdCommentWriter>(*this)),
      function_map(),
      instruction_map(),
      value_id_counter(1) {}

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

void BitcodeExplorer::ForEachFunction(const std::function<void(ValueId, llvm::Function &)> &callback, bool include_generated) {
    for (const auto &[function_id, weak_vh] : function_map) {
        if (llvm::Function *function = cast_or_null<llvm::Function>(weak_vh)) {
            if (include_generated || (GetSourceId(*function) == GetExplorerId(*function))) {
                callback(function_id, *function);
            }
        }
    }
}

bool BitcodeExplorer::PrintFunction(ValueId function_id, llvm::raw_ostream &output_stream) {
    auto function_pair = function_map.find(function_id);
    if (function_pair != function_map.end()) {
        if (llvm::Function *function = cast_or_null<llvm::Function>(function_pair->second)) {
            function->print(output_stream, annotator.get());
            return true;
        }
    }
    return false;
}

ValueId BitcodeExplorer::InlineFunctionCall(ValueId instruction_id, const std::function<void(llvm::Function *)> &resolve_declaration, const std::function<llvm::Value *(llvm::Use *, llvm::Value *)> &substitute_value_func) {
    auto instruction_pair = instruction_map.find(instruction_id);
    if (instruction_pair != instruction_map.end()) {
        if (llvm::Instruction *instruction = cast_or_null<llvm::Instruction>(instruction_pair->second)) {
            if (auto call_base = llvm::dyn_cast<llvm::CallBase>(instruction)) {
                llvm::Function *called_function = call_base->getCalledFunction();
                llvm::Module *func_module = called_function->getParent();

                // try to resolve declarations
                if (called_function->isDeclaration()) {
                    resolve_declaration(called_function);
                    UpdateMetadata(*called_function);
                    if (called_function->isDeclaration()) { return 0; }
                }

                llvm::Function *caller_function = call_base->getFunction();
                assert(caller_function != nullptr);

                // clone and modify the called function prior to inlining
                llvm::ValueToValueMapTy called_value_map;
                llvm::Function *cloned_called_function = llvm::CloneFunction(called_function, called_value_map);

                llvm::BasicBlock &entry = cloned_called_function->getEntryBlock();
                llvm::IRBuilder<> builder(&entry);
                builder.SetInsertPoint(&entry, entry.begin());
                // add hook for each argument
                for (llvm::Argument &arg : cloned_called_function->args()) {
                    llvm::FunctionType *func_type = llvm::FunctionType::get(
                            arg.getType(), {arg.getType()},
                            false);
                    llvm::FunctionCallee hook = func_module->getOrInsertFunction(GetSubstituteHookName(arg.getType()->getTypeID()), func_type);
                    llvm::CallInst *call_inst = builder.CreateCall(hook, {&arg}, "temp_val");

                    arg.replaceUsesWithIf(call_inst, [call_inst](const llvm::Use &use) -> bool {
                        return use.getUser() != call_inst;
                    });
                }

                // clone and modify the caller function
                llvm::ValueToValueMapTy caller_value_map;
                llvm::Function *cloned_caller_function = llvm::CloneFunction(caller_function, caller_value_map);

                llvm::CallBase *cloned_call_base = nullptr;
                for (auto &cloned_instruction : llvm::instructions(cloned_caller_function)) {
                    if (this->GetExplorerId(cloned_instruction) == instruction_id) {
                        cloned_call_base = llvm::dyn_cast<llvm::CallBase>(&cloned_instruction);
                    }
                }
                assert(cloned_call_base != nullptr);

                // hook the function call
                auto *dup_call_base = dyn_cast<llvm::CallBase>(cloned_call_base->clone());
                dup_call_base->setName("temp_val");
                dup_call_base->setCalledFunction(cloned_called_function);
                dup_call_base->insertBefore(cloned_call_base);

                std::string original_name = cloned_call_base->getName().str();
                cloned_call_base->setName("to_delete");

                llvm::FunctionType *func_type = llvm::FunctionType::get(
                        cloned_call_base->getType(), {cloned_call_base->getType()},
                        false);
                llvm::FunctionCallee hook = func_module->getOrInsertFunction(GetSubstituteHookName(cloned_call_base->getType()->getTypeID()), func_type);
                llvm::CallInst *substituted_call = llvm::CallInst::Create(hook, { dup_call_base }, original_name, cloned_call_base);

                cloned_call_base->replaceAllUsesWith(substituted_call);
                cloned_call_base->eraseFromParent();

                // Do the inlining
                llvm::InlineFunctionInfo info;
                llvm::InlineFunction(*dup_call_base, info);

                // delete the cloned copy of the called_function after inlining it
                cloned_called_function->eraseFromParent();

                // elide the hooks
                ElideSubstitutionHooks(*cloned_caller_function, substitute_value_func);

                // update all metadata
                UpdateMetadata(*cloned_caller_function);

                return GetExplorerId(*cloned_caller_function);
            }
        }
    }
    return 0;
}

std::string BitcodeExplorer::GetSubstituteHookName(llvm::Type::TypeID type_id) {
    return llvm::formatv("substitute_hook_{0}", type_id).str();
}

// Get an id from the metadata of a function. Returns 0 if the id does not exist.
ValueId BitcodeExplorer::GetIntMetadata(const llvm::Function &function, unsigned kind_id) {
    llvm::MDNode* mdnode = function.getMetadata(kind_id);
    if (mdnode == nullptr) { return 0; }
    ValueId id = cast<llvm::ConstantInt>(cast<llvm::ConstantAsMetadata>(mdnode->getOperand(0))->getValue())->getZExtValue();
    return id;
}

// Get an id from the metadata of an instruction. Returns 0 if the id does not exist.
ValueId BitcodeExplorer::GetIntMetadata(const llvm::Instruction &instruction, unsigned kind_id) {
    llvm::MDNode* mdnode = instruction.getMetadata(kind_id);
    if (mdnode == nullptr) { return 0; }
    ValueId id = cast<llvm::ConstantInt>(cast<llvm::ConstantAsMetadata>(mdnode->getOperand(0))->getValue())->getZExtValue();
    return id;
}

// Set an id inside the metadata of a function.
void BitcodeExplorer::SetIntMetadata(llvm::Function &function, ValueId value, unsigned kind_id) const {
    llvm::MDNode *mdnode = llvm::MDNode::get(llvm_context,
                                             llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                                                     llvm_context,
                                                     llvm::APInt(64, value, false))));
    function.setMetadata(kind_id, mdnode);
}

// Set an id inside the metadata of an instruction.
void BitcodeExplorer::SetIntMetadata(llvm::Instruction &instruction, ValueId value, unsigned kind_id) const {
    llvm::MDNode *mdnode = llvm::MDNode::get(llvm_context,
                                             llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(
                                                     llvm_context,
                                                     llvm::APInt(64, value, false))));
    instruction.setMetadata(kind_id, mdnode);
}

ValueId BitcodeExplorer::GetExplorerId(const llvm::Function &function) const {
    return GetIntMetadata(function, md_explorer_id);
}

ValueId BitcodeExplorer::GetExplorerId(const llvm::Instruction &instruction) const {
    return GetIntMetadata(instruction, md_explorer_id);
}

void BitcodeExplorer::SetExplorerId(llvm::Function &function, ValueId value) const {
    SetIntMetadata(function, value, md_explorer_id);
}

void BitcodeExplorer::SetExplorerId(llvm::Instruction &instruction, ValueId value) const {
    SetIntMetadata(instruction, value, md_explorer_id);
}

ValueId BitcodeExplorer::GetSourceId(const llvm::Function &function) const {
    return GetIntMetadata(function, md_explorer_source_id);
}

ValueId BitcodeExplorer::GetSourceId(const llvm::Instruction &instruction) const {
    return GetIntMetadata(instruction, md_explorer_source_id);
}

void BitcodeExplorer::SetSourceId(llvm::Function &function, ValueId value) const {
    SetIntMetadata(function, value, md_explorer_source_id);
}

void BitcodeExplorer::SetSourceId(llvm::Instruction &instruction, ValueId value) const {
    SetIntMetadata(instruction, value, md_explorer_source_id);
}

void BitcodeExplorer::UpdateMetadata(llvm::Function &function) {
    ValueId function_id = value_id_counter++;
    SetExplorerId(function, function_id);
    if (GetSourceId(function) == 0) {
        SetSourceId(function, function_id);
    }
    function_map.emplace(function_id, &function);

    for (auto &instruction : llvm::instructions(function)) {
        ValueId new_instruction_id = value_id_counter++;
        SetExplorerId(instruction, new_instruction_id);

        // for an instruction without a source, set itself to be the self
        if (GetSourceId(instruction) == 0) {
            SetSourceId(instruction, new_instruction_id);
        }

        instruction_map.emplace(new_instruction_id, &instruction);
    }
}

const llvm::Type::TypeID possible_type_ids[] = {
        llvm::Type::HalfTyID,
        llvm::Type::BFloatTyID,
        llvm::Type::FloatTyID,
        llvm::Type::DoubleTyID,
        llvm::Type::X86_FP80TyID,
        llvm::Type::FP128TyID,
        llvm::Type::PPC_FP128TyID,
        llvm::Type::VoidTyID,
        llvm::Type::LabelTyID,
        llvm::Type::MetadataTyID,
        llvm::Type::X86_MMXTyID,
        llvm::Type::TokenTyID,
        llvm::Type::IntegerTyID,
        llvm::Type::FunctionTyID,
        llvm::Type::PointerTyID,
        llvm::Type::StructTyID,
        llvm::Type::ArrayTyID,
        llvm::Type::FixedVectorTyID,
        llvm::Type::ScalableVectorTyID
};

void BitcodeExplorer::ElideSubstitutionHooks(llvm::Function &function, const std::function<llvm::Value *(llvm::Use *, llvm::Value *)> &substitute_value_func) {
    llvm::Module *func_module = function.getParent();

    // find all uses of the hook functions
    std::vector<std::pair<llvm::Use *, llvm::Value *>> subs;
    for (llvm::Type::TypeID type_id : possible_type_ids) {
        if (llvm::Function *hook_func = func_module->getFunction(GetSubstituteHookName(type_id))) {
            for (llvm::Use &use : hook_func->uses()) {
                subs.emplace_back(&use, use.getUser()->getOperand(0));
            }
        }
    }

    // substitute the values
    for (auto [use, sub_val] : subs) {
        llvm::User *user = use->getUser();
        if (auto *inst = dyn_cast<llvm::Instruction>(user)) {
            inst->replaceAllUsesWith(substitute_value_func(use, sub_val));
            inst->eraseFromParent();
        }
    }

    // remove all the hook functions after eliding them
    for (llvm::Type::TypeID type_id : possible_type_ids) {
        if (llvm::Function *hook_func = func_module->getFunction(GetSubstituteHookName(type_id))) {
            hook_func->replaceAllUsesWith(llvm::UndefValue::get(hook_func->getType()));
            hook_func->eraseFromParent();
        }
    }
}

BitcodeExplorer::~BitcodeExplorer() = default;
}