/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <magnifier/BitcodeExplorer.h>

#include <magnifier/IFunctionResolver.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/InitLLVM.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <functional>
#include <fstream>

std::vector<std::string> split(const std::string &input, char delimiter) {
    std::stringstream ss(input);
    std::vector<std::string> results;
    std::string token;
    while (std::getline(ss, token, delimiter)) {
        results.push_back(token);
    }
    return results;
}

class FunctionResolver : public magnifier::IFunctionResolver {
    llvm::Function *ResolveCallSite(llvm::CallBase *call_base, llvm::Function *called_function) override {
        return called_function;
    }
};

int main(int argc, char **argv) {
    llvm::InitLLVM x(argc, argv);
    llvm::LLVMContext llvm_context;
    llvm::ExitOnError llvm_exit_on_err;
    llvm_exit_on_err.setBanner("llvm error: ");

    magnifier::BitcodeExplorer explorer(llvm_context);
    FunctionResolver resolver{};

    std::error_code error_code;
    std::unique_ptr<llvm::ToolOutputFile> tool_output = std::make_unique<llvm::ToolOutputFile>("-", error_code,llvm::sys::fs::OF_Text);
    if (error_code) {
        std::cerr << error_code.message() << std::endl;
        return -1;
    }

    std::map<std::string, std::function<void(const std::vector<std::string> &)>> cmd_map = {
            // Load module: `lm <path>`
            {"lm", [&explorer, &llvm_exit_on_err, &llvm_context, &tool_output](const std::vector<std::string> &args) -> void {
                if (args.size() != 2) {
                    tool_output->os() << "Usage: lm <path> - Load/open an LLVM .bc or .ll module\n";
                    return;
                }
                const std::string &filename = args[1];
                const std::filesystem::path file_path = std::filesystem::path(filename);

                if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path)) {
                    tool_output->os() << "Unable to open file: " << filename << "\n";
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
            // List functions: `lf`
            {"lf", [&explorer, &tool_output](const std::vector<std::string> &args) -> void {
                if (args.size() != 1) {
                    tool_output->os() << "Usage: lf - List all functions in all open modules\n";
                    return;
                }

                explorer.ForEachFunction([&tool_output](magnifier::ValueId function_id, llvm::Function &function, magnifier::FunctionKind kind) -> void {
                    if (function.hasName() && kind == magnifier::FunctionKind::kOriginal) {
                        tool_output->os() << function_id << " " << function.getName().str() << "\n";
                    }
                });
            }},
            // Print function: `pf <function_id>`
            {"pf", [&explorer, &tool_output](const std::vector<std::string> &args) -> void {
                if (args.size() != 2) {
                    tool_output->os() << "Usage: pf <function_id> - Print function\n";
                    return;
                }

                magnifier::ValueId function_id = std::stoul(args[1], nullptr, 10);
                if (!explorer.PrintFunction(function_id, tool_output->os())) {
                    tool_output->os() << "Function not found: " << function_id << "\n";
                }
            }},
            // Inline function call: `ic <instruction_id>`
            {"ic", [&explorer, &tool_output, &resolver](const std::vector<std::string> &args) -> void {
                static const std::map<magnifier::InlineError, std::string> inline_error_map = {
                        {magnifier::InlineError::kNotACallBaseInstruction, "Not a CallBase instruction"},
                        {magnifier::InlineError::kInstructionNotFound,     "Instruction not found"},
                        {magnifier::InlineError::kCannotResolveFunction,   "Cannot resolve function"},
                        {magnifier::InlineError::kInlineOperationFailed,   "Inline operation failed"},
                        {magnifier::InlineError::kVariadicFunction,        "Inlining variadic function is yet to be supported"},
                        {magnifier::InlineError::kResolveFunctionTypeMismatch, "Resolve function type mismatch"},
                };

                if (args.size() != 2) {
                    tool_output->os() << "Usage: ic <instruction_id> - Inline function call\n";
                    return;
                }

                magnifier::ValueId instruction_id = std::stoul(args[1], nullptr, 10);
                magnifier::Result<magnifier::ValueId, magnifier::InlineError> result = explorer.InlineFunctionCall(instruction_id, resolver,[&tool_output](llvm::Use *use, llvm::Value *sub_val) {
                    tool_output->os() << "user: ";
                    use->getUser()->print(tool_output->os());
                    tool_output->os() << "\n";
                    return sub_val;
                });
                if (result.Succeeded()) {
                    explorer.PrintFunction(result.Value(), tool_output->os());
                } else {
                    tool_output->os() << "Inline function call failed for id: " << instruction_id << " (error: " << inline_error_map.at(result.Error()) << ")\n";
                }
            }},
    };

    while (true) {
        std::string input;
        tool_output->os() << ">> ";
        std::getline(std::cin, input);

        std::vector<std::string> tokenized_input = split(input, ' ');
        if (tokenized_input.empty()) {
            tool_output->os() << "Invalid Command\n";
            continue;
        }

        if (tokenized_input[0] == "exit") { break; }

        std::map<std::string, std::function<void(const std::vector<std::string> &)>>::iterator cmd = cmd_map.find(tokenized_input[0]);
        if (cmd != cmd_map.end()) {
            cmd->second(tokenized_input);
        } else {
            tool_output->os() << "Invalid Command: " << tokenized_input[0] << "\n";
        }
    }
    return 0;
}