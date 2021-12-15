/*
 * Copyright (c) 2019-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#ifndef MAGNIFIER_BITCODEEXPLORER_H
#define MAGNIFIER_BITCODEEXPLORER_H

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>
#include <iterator>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/FileSystem.h"

#include "IDCommentWriter.h"

using ValueId = unsigned long;

class BitcodeExplorer {

private:

    llvm::LLVMContext &llvm_context;
    std::unique_ptr<llvm::AssemblyAnnotationWriter> annotator;
    std::vector<std::unique_ptr<llvm::Module>> opened_modules;
    std::map<ValueId, llvm::WeakVH> function_map;
    ValueId value_id_counter;

public:
    explicit BitcodeExplorer(llvm::LLVMContext &llvm_context) : function_map(), llvm_context(llvm_context), value_id_counter(0) {
        annotator = std::make_unique<IDCommentWriter>();
    }

    bool TakeModule(std::unique_ptr<llvm::Module> module);

    void ForEachFunction(const std::function<void(ValueId, llvm::Function &)>& callback);

    bool PrintFunction(ValueId function_id, llvm::raw_ostream &output_stream);
};


#endif //MAGNIFIER_BITCODEEXPLORER_H
