#ifndef MAGNIFIER_IDCOMMENTWRITER_H
#define MAGNIFIER_IDCOMMENTWRITER_H

#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/FormattedStream.h"

class IDCommentWriter : public llvm::AssemblyAnnotationWriter {
public:
    void emitInstructionAnnot(const llvm::Instruction *instruction, llvm::formatted_raw_ostream &OS) override;
    void emitFunctionAnnot(const llvm::Function *function, llvm::formatted_raw_ostream &OS) override;
};


#endif //MAGNIFIER_IDCOMMENTWRITER_H
