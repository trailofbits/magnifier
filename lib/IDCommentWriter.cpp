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

void IdCommentWriter::emitInstructionAnnot(const llvm::Instruction *instruction, llvm::formatted_raw_ostream &OS) {
    ValueId instruction_id = explorer.GetExplorerId(*instruction);
    ValueId source_id = explorer.GetSourceId(*instruction);
    OS << instruction_id << "/" << source_id;
}

void IdCommentWriter::emitFunctionAnnot(const llvm::Function *function, llvm::formatted_raw_ostream &OS) {
    ValueId function_id = explorer.GetExplorerId(*function);
    ValueId source_id = explorer.GetSourceId(*function);
    OS << function_id << "/" << source_id;
}
}

