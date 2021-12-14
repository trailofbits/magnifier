#ifndef MAGNIFIER_BITCODEEXPLORER_H
#define MAGNIFIER_BITCODEEXPLORER_H

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <fstream>
#include <iterator>

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/FileSystem.h"

#include "IDCommentWriter.h"
using fid_t = unsigned long;
class BitcodeExplorer {

private:

    llvm::LLVMContext Context;
    llvm::ExitOnError ExitOnErr;
    std::vector<std::unique_ptr<llvm::Module>> opened_modules;
    std::map<fid_t , llvm::Function*> function_map;
    fid_t function_counter;

public:
    BitcodeExplorer(): function_map(), Context(), ExitOnErr(), function_counter(0) {
        ExitOnErr.setBanner("llvm error: ");
    }
    bool OpenFile(const std::string& filename);
    void ListFunctions();
    void PrintFunction(fid_t function_id);
};


#endif //MAGNIFIER_BITCODEEXPLORER_H
