/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "IDCommentWriter.h"

#include "magnifier/BitcodeExplorer.h"
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/FormattedStream.h>


namespace magnifier {
IDCommentWriter::IDCommentWriter(unsigned md_explorer_id) : md_explorer_id(md_explorer_id) {}

void IDCommentWriter::emitInstructionAnnot(const llvm::Instruction *instruction, llvm::formatted_raw_ostream &OS) {
    OS << "XXX";
}

void IDCommentWriter::emitFunctionAnnot(const llvm::Function *function, llvm::formatted_raw_ostream &OS) {
    ValueId function_id = cast<llvm::ConstantInt>(cast<llvm::ConstantAsMetadata>(function->getMetadata(md_explorer_id)->getOperand(0))->getValue())->getZExtValue();
    OS << "id: " << function_id;
}
}

