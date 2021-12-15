/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/ValueHandle.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/ToolOutputFile.h>

#include <magnifier/BitcodeExplorer.h>
#include "IDCommentWriter.h"

BitcodeExplorer::BitcodeExplorer(llvm::LLVMContext &llvm_context)
        : llvm_context(llvm_context),
          md_explorer_id(llvm_context.getMDKindID("explorer.id")),
          annotator(std::make_unique<IDCommentWriter>(md_explorer_id)),
          function_map(),
          value_id_counter(0) {}

bool BitcodeExplorer::TakeModule(std::unique_ptr<llvm::Module> module) {
    llvm::LLVMContext &module_context = module->getContext();
    assert(std::addressof(module_context) == std::addressof(llvm_context));

    for (auto &function: module->functions()) {
        ValueId function_id = value_id_counter++;

        llvm::MDNode *mdnode = llvm::MDNode::get(module_context, llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(module_context, llvm::APInt(64, function_id, false))));
        function.setMetadata(md_explorer_id, mdnode);
        function_map.emplace(function_id, &function);
    }
    opened_modules.push_back(std::move(module));

    return true;
}

void BitcodeExplorer::ForEachFunction(const std::function<void(ValueId, llvm::Function &)> &callback) {
    for (const auto &[function_id, weak_vh]: function_map) {
        if (llvm::Function *function = cast_or_null<llvm::Function>(weak_vh)) {
            callback(function_id, *function);
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

BitcodeExplorer::~BitcodeExplorer() = default;
