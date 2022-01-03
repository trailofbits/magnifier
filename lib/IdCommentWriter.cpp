/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "IdCommentWriter.h"

#include <magnifier/BitcodeExplorer.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Constants.h>
#include <llvm/Support/FormattedStream.h>


namespace magnifier {
IdCommentWriter::IdCommentWriter(BitcodeExplorer &explorer) : explorer(explorer) {}

void IdCommentWriter::emitInstructionAnnot(const llvm::Instruction *instruction, llvm::formatted_raw_ostream &os) {
    ValueId instruction_id = explorer.GetId(*instruction, ValueIdKind::kDerived);
    ValueId source_id = explorer.GetId(*instruction, ValueIdKind::kOriginal);
    os << instruction_id << "|" << source_id;
}

void IdCommentWriter::emitFunctionAnnot(const llvm::Function *function, llvm::formatted_raw_ostream &os) {
    ValueId function_id = explorer.GetId(*function, ValueIdKind::kDerived);
    ValueId source_id = explorer.GetId(*function, ValueIdKind::kOriginal);
    os << function_id << "|" << source_id;
}

void IdCommentWriter::emitBasicBlockStartAnnot(const llvm::BasicBlock *block, llvm::formatted_raw_ostream &os) {
    const llvm::Instruction *terminator = block->getTerminator();
    if (!terminator) { return; }
    os << "--- start block: " << explorer.GetId(*terminator, ValueIdKind::kBlock) << " ---\n";
}

void IdCommentWriter::emitBasicBlockEndAnnot(const llvm::BasicBlock *block, llvm::formatted_raw_ostream &os) {
    const llvm::Instruction *terminator = block->getTerminator();
    if (!terminator) { return; }
    os << "--- end block: " << explorer.GetId(*terminator, ValueIdKind::kBlock) << " ---\n";
}
}

