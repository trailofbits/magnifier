/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#include <magnifier/BitcodeExplorer.h>

#include <magnifier/IFunctionResolver.h>
#include <magnifier/ISubstitutionObserver.h>
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
#include <unordered_map>
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

class SubstitutionObserver : public magnifier::ISubstitutionObserver {
private:
   llvm::ToolOutputFile &tool_output;
public:
    explicit SubstitutionObserver(llvm::ToolOutputFile &tool_output): tool_output(tool_output) {};

    llvm::Value *PerformSubstitution(llvm::Instruction *instr, llvm::Value *old_val, llvm::Value *new_val, magnifier::SubstitutionKind kind) override {
        static const std::unordered_map<magnifier::SubstitutionKind, std::string> substitution_kind_map = {
                {magnifier::SubstitutionKind::kReturnValue, "Return value"},
                {magnifier::SubstitutionKind::kArgument, "Argument"},
                {magnifier::SubstitutionKind::kConstantFolding, "Constant folding"},
                {magnifier::SubstitutionKind::kValueSubstitution, "Value substitution"},
                {magnifier::SubstitutionKind::kFunctionDevirtualization, "Function devirtualization"},
        };
        tool_output.os() << "perform substitution: ";
        instr->print(tool_output.os());
        tool_output.os() << " : " << substitution_kind_map.at(kind) << "\n";
        return new_val;
    }
};

void RunOptimization(magnifier::BitcodeExplorer &explorer, llvm::ToolOutputFile &tool_output, magnifier::ValueId function_id, llvm::PassBuilder::OptimizationLevel level) {
    static const std::unordered_map<magnifier::OptimizationError, std::string> optimization_error_map = {
            {magnifier::OptimizationError::kInvalidOptimizationLevel, "The provided optimization level is not allowed"},
            {magnifier::OptimizationError::kIdNotFound, "Function id not found"},
    };

    magnifier::Result<magnifier::ValueId, magnifier::OptimizationError> result = explorer.OptimizeFunction(function_id, level);
    if (result.Succeeded()) {
        explorer.PrintFunction(result.Value(), tool_output.os());
    } else {
        tool_output.os() << "Optimize function failed for id: " << function_id << " (error: " << optimization_error_map.at(result.Error()) << ")\n";
    }
}

int main(int argc, char **argv) {
    llvm::InitLLVM x(argc, argv);
    llvm::LLVMContext llvm_context;
    llvm::ExitOnError llvm_exit_on_err;
    llvm_exit_on_err.setBanner("llvm error: ");

    magnifier::BitcodeExplorer explorer(llvm_context);


    std::error_code error_code;
    llvm::ToolOutputFile tool_output("-", error_code,llvm::sys::fs::OF_Text);
    if (error_code) {
        std::cerr << error_code.message() << std::endl;
        return -1;
    }

    FunctionResolver resolver{};
    SubstitutionObserver substitution_observer(tool_output);

    std::unordered_map<std::string, std::function<void(const std::vector<std::string> &)>> cmd_map = {
            // Load module: `lm <path>`
            {"lm", [&explorer, &llvm_exit_on_err, &llvm_context, &tool_output](const std::vector<std::string> &args) -> void {
                if (args.size() != 2) {
                    tool_output.os() << "Usage: lm <path> - Load/open an LLVM .bc or .ll module\n";
                    return;
                }
                const std::string &filename = args[1];
                const std::filesystem::path file_path = std::filesystem::path(filename);

                if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path)) {
                    tool_output.os() << "Unable to open file: " << filename << "\n";
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
                    tool_output.os() << "Usage: lf - List all functions in all open modules\n";
                    return;
                }

                explorer.ForEachFunction([&tool_output](magnifier::ValueId function_id, llvm::Function &function, magnifier::FunctionKind kind) -> void {
                    if (function.hasName() && kind == magnifier::FunctionKind::kOriginal) {
                        tool_output.os() << function_id << " " << function.getName().str() << "\n";
                    }
                });
            }},
            // Print function: `pf <function_id>`
            {"pf", [&explorer, &tool_output](const std::vector<std::string> &args) -> void {
                if (args.size() != 2) {
                    tool_output.os() << "Usage: pf <function_id> - Print function\n";
                    return;
                }

                magnifier::ValueId function_id = std::stoul(args[1], nullptr, 10);
                if (!explorer.PrintFunction(function_id, tool_output.os())) {
                    tool_output.os() << "Function not found: " << function_id << "\n";
                }
            }},
            // Devirtualize function: `dc <instruction_id> <function_id>`
            {"dc", [&explorer, &tool_output, &substitution_observer](const std::vector<std::string> &args) -> void {
                static const std::unordered_map<magnifier::DevirtualizeError, std::string> devirtualize_error_map = {
                        {magnifier::DevirtualizeError::kNotACallBaseInstruction, "Not a CallBase instruction"},
                        {magnifier::DevirtualizeError::kInstructionNotFound,     "Instruction not found"},
                        {magnifier::DevirtualizeError::kFunctionNotFound,        "Function not found"},
                        {magnifier::DevirtualizeError::kNotAIndirectCall,        "Can only devirtualize indirect call"},
                        {magnifier::DevirtualizeError::kArgNumMismatch,          "Function takes a different number of parameter"}
                };

                if (args.size() != 3) {
                    tool_output.os() << "Usage: dc <instruction_id> <function_id> - Devirtualize function\n";
                    return;
                }

                magnifier::ValueId instruction_id = std::stoul(args[1], nullptr, 10);
                magnifier::ValueId function_id = std::stoul(args[2], nullptr, 10);
                magnifier::Result<magnifier::ValueId, magnifier::DevirtualizeError> result = explorer.DevirtualizeFunction(instruction_id, function_id, substitution_observer);


                if (result.Succeeded()) {
                    explorer.PrintFunction(result.Value(), tool_output.os());
                } else {
                    tool_output.os() << "Devirtualize function call failed for id: " << instruction_id << " (error: " << devirtualize_error_map.at(result.Error()) << ")\n";
                }
            }},
            // Delete function: `df! <function_id>`
            {"df!", [&explorer, &tool_output](const std::vector<std::string> &args) -> void {
                static const std::unordered_map<magnifier::DeletionError, std::string> deletion_error_map = {
                        {magnifier::DeletionError::kIdNotFound, "Function id not found"},
                        {magnifier::DeletionError::kFunctionInUse, "Function is still in use"},
                };

                if (args.size() != 2) {
                    tool_output.os() << "Usage: df! <function_id> - Delete function\n";
                    return;
                }

                magnifier::ValueId function_id = std::stoul(args[1], nullptr, 10);
                std::optional<magnifier::DeletionError> result = explorer.DeleteFunction(function_id);
                if (!result) {
                    tool_output.os() << "Deleted function with id: " << function_id << "\n";
                } else {
                    tool_output.os() << "Delete function failed for id: " << function_id << " (error: " << deletion_error_map.at(result.value()) << ")\n";
                }
            }},
            // Inline function call: `ic <instruction_id>`
            {"ic", [&explorer, &tool_output, &resolver, &substitution_observer](const std::vector<std::string> &args) -> void {
                static const std::unordered_map<magnifier::InlineError, std::string> inline_error_map = {
                        {magnifier::InlineError::kNotACallBaseInstruction, "Not a CallBase instruction"},
                        {magnifier::InlineError::kInstructionNotFound,     "Instruction not found"},
                        {magnifier::InlineError::kCannotResolveFunction,   "Cannot resolve function"},
                        {magnifier::InlineError::kInlineOperationFailed,   "Inline operation failed"},
                        {magnifier::InlineError::kVariadicFunction,        "Inlining variadic function is yet to be supported"},
                        {magnifier::InlineError::kResolveFunctionTypeMismatch, "Resolve function type mismatch"},
                };

                if (args.size() != 2) {
                    tool_output.os() << "Usage: ic <instruction_id> - Inline function call\n";
                    return;
                }

                magnifier::ValueId instruction_id = std::stoul(args[1], nullptr, 10);
                magnifier::Result<magnifier::ValueId, magnifier::InlineError> result = explorer.InlineFunctionCall(instruction_id, resolver, substitution_observer);

                if (result.Succeeded()) {
                    explorer.PrintFunction(result.Value(), tool_output.os());
                } else {
                    tool_output.os() << "Inline function call failed for id: " << instruction_id << " (error: " << inline_error_map.at(result.Error()) << ")\n";
                }
            }},
            // Substitute with value: `sv <id> <val>`
            {"sv", [&explorer, &tool_output, &substitution_observer](const std::vector<std::string> &args) -> void {
                static const std::unordered_map<magnifier::SubstitutionError, std::string> substitution_error_map = {
                        {magnifier::SubstitutionError::kIdNotFound,    "Instruction not found"},
                        {magnifier::SubstitutionError::kIncorrectType, "Instruction has non-integer type"},
                        {magnifier::SubstitutionError::kCannotUseFunctionId, "Expecting an instruction id instead of a function id"},
                };

                if (args.size() != 3) {
                    std::cout << "Usage: sv <id> <val> - Substitute with value" << std::endl;
                    return;
                }

                magnifier::ValueId value_id = std::stoul(args[1], nullptr, 10);
                uint64_t value = std::stoul(args[2], nullptr, 10);

                // Try treating `value_id` as an instruction id
                magnifier::Result<magnifier::ValueId, magnifier::SubstitutionError> result = explorer.SubstituteInstructionWithValue(value_id, value, substitution_observer);

                if (result.Succeeded()) {
                    explorer.PrintFunction(result.Value(), tool_output.os());
                    return;
                }

                if (result.Error() != magnifier::SubstitutionError::kIdNotFound) {
                    tool_output.os() << "Substitute value failed for id:  " << value_id << " (error: " << substitution_error_map.at(result.Error()) << ")\n";
                    return;
                }

                // Try treating `value_id` as an argument id
                result = explorer.SubstituteArgumentWithValue(value_id, value, substitution_observer);

                if (result.Succeeded()) {
                    explorer.PrintFunction(result.Value(), tool_output.os());
                } else {
                    tool_output.os() << "Substitute value failed for id:  " << value_id << " (error: " << substitution_error_map.at(result.Error()) << ")\n";
                }
            }},
            // Optimize function bitcode using optimization level -O1: `o1 <id>`
            {"o1", [&explorer, &tool_output, &substitution_observer](const std::vector<std::string> &args) -> void {
                if (args.size() != 2) {
                    std::cout << "Usage: o1 <id> - Optimize function bitcode using optimization level -O1" << std::endl;
                    return;
                }

                magnifier::ValueId function_id = std::stoul(args[1], nullptr, 10);
                RunOptimization(explorer, tool_output, function_id, llvm::PassBuilder::OptimizationLevel::O1);
            }},
            // Optimize function bitcode using optimization level -O2: `o2 <id>`
            {"o2", [&explorer, &tool_output, &substitution_observer](const std::vector<std::string> &args) -> void {
                if (args.size() != 2) {
                    std::cout << "Usage: o2 <id> - Optimize function bitcode using optimization level -O2" << std::endl;
                    return;
                }

                magnifier::ValueId function_id = std::stoul(args[1], nullptr, 10);
                RunOptimization(explorer, tool_output, function_id, llvm::PassBuilder::OptimizationLevel::O2);
            }},
            // Optimize function bitcode using optimization level -O3: `o3 <id>`
            {"o3", [&explorer, &tool_output, &substitution_observer](const std::vector<std::string> &args) -> void {
                if (args.size() != 2) {
                    std::cout << "Usage: o3 <id> - Optimize function bitcode using optimization level -O3" << std::endl;
                    return;
                }

                magnifier::ValueId function_id = std::stoul(args[1], nullptr, 10);
                RunOptimization(explorer, tool_output, function_id, llvm::PassBuilder::OptimizationLevel::O3);
            }},
    };

//    cmd_map["lm"](split("lm ../test.bc", ' '));
    while (true) {
        std::string input;
        tool_output.os() << ">> ";
        std::getline(std::cin, input);

        std::vector<std::string> tokenized_input = split(input, ' ');
        if (tokenized_input.empty()) {
            tool_output.os() << "Invalid Command\n";
            continue;
        }

        if (tokenized_input[0] == "exit") { break; }

        auto cmd = cmd_map.find(tokenized_input[0]);
        if (cmd != cmd_map.end()) {
            cmd->second(tokenized_input);
        } else {
            tool_output.os() << "Invalid Command: " << tokenized_input[0] << "\n";
        }
    }
    return 0;
}