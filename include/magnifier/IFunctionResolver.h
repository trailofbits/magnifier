/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

namespace llvm {
class Function;
class CallBase;
}

namespace magnifier {

class IFunctionResolver {
public:
    virtual llvm::Function *ResolveCallSite(llvm::CallBase *call_base, llvm::Function *called_function) = 0;
};

}