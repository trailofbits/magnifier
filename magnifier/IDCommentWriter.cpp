#include "IDCommentWriter.h"

void IDCommentWriter::emitInstructionAnnot(const llvm::Instruction *instruction, llvm::formatted_raw_ostream &OS) {
    OS << "XXX";
}

void IDCommentWriter::emitFunctionAnnot(const llvm::Function *function, llvm::formatted_raw_ostream &OS) {
    long long function_id = cast<llvm::ConstantInt>(cast<llvm::ConstantAsMetadata>(function->getMetadata("explorer.id")->getOperand(0))->getValue())->getSExtValue();
    OS << "id: " << function_id;
}
