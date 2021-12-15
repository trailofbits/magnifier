/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */


#pragma once

#include <llvm/IR/AssemblyAnnotationWriter.h>

namespace llvm {
    class Instruction;
    class formatted_raw_ostream;
    class Function;
} // namespace llvm

class IDCommentWriter : public llvm::AssemblyAnnotationWriter {
private:
    unsigned md_explorer_id;
public:
    explicit IDCommentWriter(unsigned md_explorer_id);

    void emitInstructionAnnot(const llvm::Instruction *instruction, llvm::formatted_raw_ostream &OS) override;

    void emitFunctionAnnot(const llvm::Function *function, llvm::formatted_raw_ostream &OS) override;
};