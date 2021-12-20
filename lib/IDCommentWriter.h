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

namespace magnifier {
class BitcodeExplorer;
class IdCommentWriter : public llvm::AssemblyAnnotationWriter {
private:
    BitcodeExplorer &explorer;
public:
    explicit IdCommentWriter(BitcodeExplorer &explorer);

    void emitInstructionAnnot(const llvm::Instruction *instruction, llvm::formatted_raw_ostream &OS) override;

    void emitFunctionAnnot(const llvm::Function *function, llvm::formatted_raw_ostream &OS) override;
};
}