/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

namespace llvm {
class Value;
class Use;
}

namespace magnifier {

enum class SubstitutionKind {
    kReturnValue = 1,
    kArgument,
    kConstantFolding,
    kValueSubstitution,
};

class ISubstitutionObserver {
public:
    virtual llvm::Value *PerformSubstitution(llvm::Use *use, llvm::Value *old_val, llvm::Value *new_val, SubstitutionKind kind) = 0;
};

}