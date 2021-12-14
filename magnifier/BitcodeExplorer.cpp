#include "BitcodeExplorer.h"


bool BitcodeExplorer::OpenFile(const std::string &filename) {
    std::ifstream bitcode_file(filename);
    if (!bitcode_file.good()) {
        std::cout << "Unable to open " << filename << std::endl;
        return false;
    }

    std::unique_ptr<llvm::MemoryBuffer> MB = ExitOnErr(errorOrToExpected(llvm::MemoryBuffer::getFileOrSTDIN(filename)));
    llvm::BitcodeFileContents IF = ExitOnErr(llvm::getBitcodeFileContents(*MB));

    for (auto &llvm_mod: IF.Mods) {
        std::unique_ptr<llvm::Module> mod = ExitOnErr(llvm_mod.parseModule(Context));

        for (auto &function: mod->functions()) {
            fid_t function_id = function_counter++;

            llvm::LLVMContext &C = function.getContext();
            llvm::MDNode *N = llvm::MDNode::get(C, llvm::ConstantAsMetadata::get(
                    llvm::ConstantInt::get(C, llvm::APInt(64, function_id, false))));
            function.setMetadata("explorer.id", N);
            function_map[function_id] = &function;
        }
        opened_modules.push_back(std::move(mod));

    }

    return true;
}

void BitcodeExplorer::ListFunctions() {
    for (const auto& mod : opened_modules) {
        for (auto& function : mod->functions()) {
            if (function.hasName()) {
                long long function_id = cast<llvm::ConstantInt>(cast<llvm::ConstantAsMetadata>(function.getMetadata("explorer.id")->getOperand(0))->getValue())->getSExtValue();
                std::cout << function_id << " " << function.getName().str() << std::endl;
            }
        }
    }
}

void BitcodeExplorer::PrintFunction(fid_t function_id) {
    auto function_pair = function_map.find(function_id);
    if (function_pair != function_map.end()) {
        llvm::Function *function = function_pair->second;

        std::unique_ptr<llvm::AssemblyAnnotationWriter> annotator = std::make_unique<IDCommentWriter>();

        std::error_code EC;
        std::unique_ptr<llvm::ToolOutputFile> output = std::make_unique<llvm::ToolOutputFile>("-", EC,
                                                                                              llvm::sys::fs::OF_Text);
        if (EC) {
            llvm::errs() << EC.message() << '\n';
            return;
        }
        function->print(output->os(), annotator.get());
    } else std::cout << "Function not found: " << function_id << std::endl;
}
