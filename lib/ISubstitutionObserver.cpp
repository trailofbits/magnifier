#include <magnifier/ISubstitutionObserver.h>

namespace magnifier {

llvm::Value *NullSubstitutionObserver::PerformSubstitution(
    llvm::Instruction *instr, llvm::Value *old_val, llvm::Value *new_val,
    SubstitutionKind kind) {
  return new_val;
}

}  // namespace magnifier