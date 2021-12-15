/*
 * Copyright (c) 2019-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "BitcodeExplorer.h"


bool BitcodeExplorer::TakeModule(std::unique_ptr<llvm::Module> module) {
    for (auto &function: module->functions()) {
        ValueId function_id = value_id_counter++;

        llvm::LLVMContext &C = function.getContext();
        llvm::MDNode *N = llvm::MDNode::get(C, llvm::ConstantAsMetadata::get(
                llvm::ConstantInt::get(C, llvm::APInt(64, function_id, false))));
        function.setMetadata("explorer.id", N);
        function_map[function_id] = &function;
    }
    opened_modules.push_back(std::move(module));

    return true;
}

void BitcodeExplorer::ForEachFunction(const std::function<void(ValueId, llvm::Function &)>& callback) {
    for (auto[function_id, weak_vh]: function_map) {
        callback(function_id, cast<llvm::Function>(*weak_vh));
    }
}

bool BitcodeExplorer::PrintFunction(ValueId function_id, llvm::raw_ostream &output_stream) {
    auto function_pair = function_map.find(function_id);
    if (function_pair != function_map.end()) {
        llvm::Function &function = cast<llvm::Function>(*function_pair->second);

        function.print(output_stream, annotator.get());
        return true;
    } else {
        output_stream << "Function not found: " << function_id << "\n";
        return false;
    }
}
