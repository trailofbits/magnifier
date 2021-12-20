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
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/ToolOutputFile.h>
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

ValueId BitcodeExplorer::InlineFunctionCall(ValueId instruction_id, const std::function<void(llvm::Function *)> &resolve_declaration) {
    auto instruction_pair = instruction_map.find(instruction_id);
    if (instruction_pair != instruction_map.end()) {
        if (llvm::Instruction *instruction = cast_or_null<llvm::Instruction>(instruction_pair->second)) {
            if (auto call_base = llvm::dyn_cast<llvm::CallBase>(instruction)) {
                llvm::Function *called_function = call_base->getCalledFunction();

                // try to resolve declarations
                if (called_function->isDeclaration()) {
                    resolve_declaration(called_function);
                    UpdateMetadata(*called_function);
                    if (called_function->isDeclaration()) { return 0; }
                }

                llvm::Function *caller_function = call_base->getFunction();
                assert(caller_function != nullptr);

                llvm::Function *cloned_function = WithClonedFunction(caller_function, [&instruction_id, this](llvm::Function *cloned_function) -> void {
                    llvm::CallBase *cloned_call_base = nullptr;
                    for (auto &instruction : llvm::instructions(cloned_function)) {
                        if (this->GetExplorerId(instruction) == instruction_id) {
                            cloned_call_base = llvm::dyn_cast<llvm::CallBase>(&instruction);
                        }
                    }
                    assert(cloned_call_base != nullptr);

                    llvm::InlineFunctionInfo info;
                    llvm::InlineFunction(*cloned_call_base, info);
                });

                return GetExplorerId(*cloned_function);
            }
        }
    }
    return 0;
}

// Runs callback on a cloned copy of the function and fixes up metadata after the call
llvm::Function *BitcodeExplorer::WithClonedFunction(llvm::Function *function,
                                                    const std::function<void(llvm::Function *)> &callback) {
    llvm::ValueToValueMapTy value_map;
    llvm::Function *cloned_function = llvm::CloneFunction(function, value_map);

    callback(cloned_function);

    // update metadata
    UpdateMetadata(*cloned_function);

    return cloned_function;
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

BitcodeExplorer::~BitcodeExplorer() = default;
}