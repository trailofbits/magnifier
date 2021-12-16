/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */


#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace llvm {
class AssemblyAnnotationWriter;
class Function;
class LLVMContext;
class Module;
class raw_ostream;
class WeakVH;
}  // namespace llvm

namespace magnifier {
class IDCommentWriter;

using ValueId = uint64_t;

class BitcodeExplorer {

private:

    const llvm::LLVMContext &llvm_context;
    const unsigned md_explorer_id;
    const std::unique_ptr<llvm::AssemblyAnnotationWriter> annotator;
    std::vector<std::unique_ptr<llvm::Module>> opened_modules;
    std::map<ValueId, llvm::WeakVH> function_map;
    ValueId value_id_counter;

public:
    explicit BitcodeExplorer(llvm::LLVMContext &llvm_context);

    ~BitcodeExplorer();

    void TakeModule(std::unique_ptr<llvm::Module> module);

    void ForEachFunction(const std::function<void(ValueId, llvm::Function &)> &callback);

    bool PrintFunction(ValueId function_id, llvm::raw_ostream &output_stream);
};
}