/*
 * Copyright (c) 2019-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include "llvm/Support/InitLLVM.h"
#include "../magnifier/BitcodeExplorer.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <functional>

std::vector<std::string> split(const std::string &input, char delimiter) {
    std::stringstream ss(input);
    std::vector<std::string> results;
    std::string token;
    while (std::getline(ss, token, delimiter))
        results.push_back(token);
    return results;
}

int main(int argc, char **argv) {
    llvm::InitLLVM x(argc, argv);
    llvm::LLVMContext llvm_context;
    llvm::ExitOnError llvm_exit_on_err;
    llvm_exit_on_err.setBanner("llvm error: ");

    BitcodeExplorer explorer(llvm_context);

    std::error_code error_code;
    std::unique_ptr<llvm::ToolOutputFile> tool_output = std::make_unique<llvm::ToolOutputFile>("-", error_code,
                                                                                               llvm::sys::fs::OF_Text);
    if (error_code) {
        llvm::errs() << error_code.message() << '\n';
        return -1;
    }

    std::map<std::string, std::function<void(const std::vector<std::string> &)>> cmd_map = {
            {"lm", [&explorer, &llvm_exit_on_err, &llvm_context](const std::vector<std::string> &args) -> void {
                if (args.size() != 2) {
                    std::cout << "Usage: lm <path> - Load/open an LLVM .bc or .ll module" << std::endl;
                    return;
                }
                const std::string &filename = args[1];
                const std::filesystem::path file_path = std::filesystem::path(filename);

                if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path)) {
                    std::cout << "Unable to open file: " << filename << std::endl;
                    return;
                }

                std::unique_ptr<llvm::MemoryBuffer> llvm_memory_buffer = llvm_exit_on_err(
                        errorOrToExpected(llvm::MemoryBuffer::getFileOrSTDIN(filename)));
                llvm::BitcodeFileContents llvm_bitcode_contents = llvm_exit_on_err(
                        llvm::getBitcodeFileContents(*llvm_memory_buffer));

                for (auto &llvm_mod: llvm_bitcode_contents.Mods) {
                    std::unique_ptr<llvm::Module> mod = llvm_exit_on_err(llvm_mod.parseModule(llvm_context));
                    explorer.TakeModule(std::move(mod));
                }
            }},
            {"lf", [&explorer](const std::vector<std::string> &args) -> void {
                if (args.size() != 1) {
                    std::cout << "Usage: lf - List all functions in all open modules" << std::endl;
                    return;
                }

                explorer.ForEachFunction([](ValueId function_id, llvm::Function &function) -> void {
                    if (function.hasName()) {
                        std::cout << function_id << " " << function.getName().str() << std::endl;
                    }
                });
            }},
            {"pf", [&explorer, &tool_output](const std::vector<std::string> &args) -> void {
                if (args.size() != 2) {
                    std::cout << "Usage: pf <function_id> - Print function" << std::endl;
                    return;
                }

                explorer.PrintFunction(std::stoul(args[1], nullptr, 10), tool_output->os());
            }},
    };

    while (true) {
        std::string input;
        std::cout << ">> ";
        std::getline(std::cin, input);

        std::vector<std::string> tokenized_input = split(input, ' ');
        if (tokenized_input.empty()) {
            std::cout << "Invalid Command" << std::endl;
            continue;
        }

        if (tokenized_input[0] == "exit") break;

        std::map<std::string, std::function<void(const std::vector<std::string> &)>>::iterator cmd = cmd_map.find(
                tokenized_input[0]);
        if (cmd != cmd_map.end()) cmd->second(tokenized_input);
        else std::cout << "Invalid Command: " << tokenized_input[0] << std::endl;


    }
    return 0;
}