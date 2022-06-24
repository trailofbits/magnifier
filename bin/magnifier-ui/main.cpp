/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */



#include <magnifier/IFunctionResolver.h>
#include <magnifier/ISubstitutionObserver.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/AssemblyAnnotationWriter.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instruction.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/FormattedStream.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/ToolOutputFile.h>
#include <llvm/Transforms/Utils/Cloning.h>

#include <uwebsockets/App.h>
#include <rellic/Decompiler.h>
#include <rellic/BC/Util.h>
#include <magnifier/BitcodeExplorer.h>


#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <functional>
#include <fstream>
#include <charconv>
#include "Printer.h"

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
    llvm::raw_ostream &output_stream;
public:
    explicit SubstitutionObserver(llvm::raw_ostream &output_stream): output_stream(output_stream) {};

    llvm::Value *PerformSubstitution(llvm::Instruction *instr, llvm::Value *old_val, llvm::Value *new_val, magnifier::SubstitutionKind kind) override {
        static const std::unordered_map<magnifier::SubstitutionKind, std::string> substitution_kind_map = {
                {magnifier::SubstitutionKind::kReturnValue, "Return value"},
                {magnifier::SubstitutionKind::kArgument, "Argument"},
                {magnifier::SubstitutionKind::kConstantFolding, "Constant folding"},
                {magnifier::SubstitutionKind::kValueSubstitution, "Value substitution"},
                {magnifier::SubstitutionKind::kFunctionDevirtualization, "Function devirtualization"},
        };
        output_stream << "perform substitution: ";
        instr->print(output_stream);
        output_stream << " : " << substitution_kind_map.at(kind) << "\n";
        return new_val;
    }
};

class AAW : public llvm::AssemblyAnnotationWriter {
private:
    magnifier::BitcodeExplorer &explorer;
public:
    explicit AAW(magnifier::BitcodeExplorer &explorer) : explorer(explorer) {}

    void emitInstructionAnnot(const llvm::Instruction *instruction, llvm::formatted_raw_ostream &os) override {
        os << "</span><span class=\"llvm\" id=\"";
        os.write_hex((unsigned long long)instruction);
        os << "\">";

        magnifier::ValueId instruction_id = explorer.GetId(*instruction, magnifier::ValueIdKind::kDerived);
        magnifier::ValueId source_id = explorer.GetId(*instruction, magnifier::ValueIdKind::kOriginal);

        os << instruction_id << "|" << source_id;
    }

    void emitFunctionAnnot(const llvm::Function *function, llvm::formatted_raw_ostream &os) override {
        os << "</span><span class=\"llvm\" id=\"";
        os.write_hex((unsigned long long)function);
        os << "\">";

        magnifier::ValueId function_id = explorer.GetId(*function, magnifier::ValueIdKind::kDerived);
        magnifier::ValueId source_id = explorer.GetId(*function, magnifier::ValueIdKind::kOriginal);

        if (!function->arg_empty()) {
            os << "Function argument ids: ";
            for (const llvm::Argument &argument : function->args()) {
                os << "(%" << argument.getName().str() << " = " << (function_id+argument.getArgNo()+1) << ") ";
            }
            os << "\n";
        }

        os << function_id << "|" << source_id;
    }

    void emitBasicBlockStartAnnot(const llvm::BasicBlock *block, llvm::formatted_raw_ostream &os) override {
        os << "</span><span>";

        const llvm::Instruction *terminator = block->getTerminator();
        if (!terminator) { return; }
        os << "--- start block: " << explorer.GetId(*terminator, magnifier::ValueIdKind::kBlock) << " ---\n";
    }

    void emitBasicBlockEndAnnot(const llvm::BasicBlock *block, llvm::formatted_raw_ostream &os) override {
        const llvm::Instruction *terminator = block->getTerminator();
        if (!terminator) { return; }
        os << "--- end block: " << explorer.GetId(*terminator, magnifier::ValueIdKind::kBlock) << " ---\n";

        os << "</span><span>";
    }

    void printInfoComment(const llvm::Value&, llvm::formatted_raw_ostream& os) override {
        os << "</span><span>";
    }
};

struct UserData {
    std::unique_ptr<llvm::LLVMContext> llvm_context;
    std::unique_ptr<magnifier::BitcodeExplorer> explorer;
    std::unique_ptr<rellic::DecompilationResult> rellic_result;
};


std::string JsonToString(llvm::json::Object &&value) {
    std::string s;
    llvm::raw_string_ostream os(s);
    os << llvm::json::Value(std::move(value));
    os.flush();
    return s;
}

llvm::json::Value GetRellicProvenance(rellic::DecompilationResult &result) {
    llvm::json::Array stmt_provenance;
    for (auto elem : result.stmt_provenance_map) {
        stmt_provenance.push_back(llvm::json::Array(
                {(unsigned long long)elem.first, (unsigned long long)elem.second}));
    }

    llvm::json::Array type_decls;
    for (auto elem : result.type_to_decl_map) {
        type_decls.push_back(llvm::json::Array(
                {(unsigned long long)elem.first, (unsigned long long)elem.second}));
    }

    llvm::json::Array value_decls;
    for (auto elem : result.value_to_decl_map) {
        value_decls.push_back(llvm::json::Array(
                {(unsigned long long)elem.first, (unsigned long long)elem.second}));
    }

    llvm::json::Array use_provenance;
    for (auto elem : result.use_expr_map) {
        if (!elem.second) {
            continue;
        }
        use_provenance.push_back(
                llvm::json::Array({(unsigned long long)elem.first,
                                   (unsigned long long)elem.second}));
    }

    llvm::json::Object msg{{"stmt_provenance", std::move(stmt_provenance)},
                           {"type_decls", std::move(type_decls)},
                           {"value_decls", std::move(value_decls)},
                           {"use_provenance", std::move(use_provenance)}};
    return msg;
}

void RunOptimization(magnifier::BitcodeExplorer &explorer, llvm::raw_ostream &tool_output, magnifier::ValueId function_id, llvm::OptimizationLevel level) {
    static const std::unordered_map<magnifier::OptimizationError, std::string> optimization_error_map = {
            {magnifier::OptimizationError::kInvalidOptimizationLevel, "The provided optimization level is not allowed"},
            {magnifier::OptimizationError::kIdNotFound, "Function id not found"},
    };

    magnifier::Result<magnifier::ValueId, magnifier::OptimizationError> result = explorer.OptimizeFunction(function_id, level);
    if (result.Succeeded()) {
        explorer.PrintFunction(result.Value(), tool_output);
    } else {
        tool_output << "Optimize function failed for id: " << function_id << " (error: " << optimization_error_map.at(result.Error()) << ")\n";
    }
}



llvm::json::Object HandleRequest(UserData *data, const llvm::json::Object &json) {
    static std::unordered_map<std::string, std::function<llvm::json::Value(UserData *, const llvm::json::Object &, const std::vector<std::string> &)>> cmd_map = {
            // Load module: `lm <path>`
            // {"lm", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
            //     llvm::ExitOnError llvm_exit_on_err;
            //     llvm_exit_on_err.setBanner("llvm error: ");

            //     if (args.size() != 2) {
            //         return "Usage: lm <path> - Load/open an LLVM .bc or .ll module\n";
            //     }
            //     const std::string &filename = args[1];
            //     const std::filesystem::path file_path = std::filesystem::path(filename);

            //     if (!std::filesystem::exists(file_path) || !std::filesystem::is_regular_file(file_path)) {
            //         return "Unable to open file: " + filename + "\n";
            //     }

            //     std::unique_ptr<llvm::MemoryBuffer> llvm_memory_buffer = llvm_exit_on_err(
            //             errorOrToExpected(llvm::MemoryBuffer::getFileOrSTDIN(filename)));
            //     llvm::BitcodeFileContents llvm_bitcode_contents = llvm_exit_on_err(
            //             llvm::getBitcodeFileContents(*llvm_memory_buffer));

            //     for (auto &llvm_mod: llvm_bitcode_contents.Mods) {
            //         std::unique_ptr<llvm::Module> mod = llvm_exit_on_err(llvm_mod.parseModule(*data->llvm_context));
            //         data->explorer->TakeModule(std::move(mod));
            //     }
            //     return "Successfully loaded: " + filename + "\n";
            // }},
            // List functions: `lf`
            {"lf", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
                if (args.size() != 1) {
                    return "Usage: lf - List all functions in all open modules\n";
                }

                std::string tool_str;
                llvm::raw_string_ostream tool_output(tool_str);

                data->explorer->ForEachFunction([&tool_output](magnifier::ValueId function_id, llvm::Function &function, magnifier::FunctionKind kind) -> void {
                    if (function.hasName() && kind == magnifier::FunctionKind::kOriginal) {
                        tool_output << function_id << " " << function.getName().str() << "\n";
                    }
                });

                tool_output.flush();
                return tool_str;
            }},
            // List all functions including generated ones: `lfa`
            {"lfa", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
                if (args.size() != 1) {
                    return "Usage: lf - List all functions in all open modules\n";
                }

                std::string tool_str;
                llvm::raw_string_ostream tool_output(tool_str);

                data->explorer->ForEachFunction([&tool_output](magnifier::ValueId function_id, llvm::Function &function, magnifier::FunctionKind kind) -> void {
                    if (function.hasName()) {
                        tool_output << function_id << " " << function.getName().str() << "\n";
                    }
                });

                tool_output.flush();
                return tool_str;
            }},
            // Print function: `pf <function_id>`
            {"pf", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
                if (args.size() != 2) {
                    return "Usage: pf <function_id> - Print function\n";
                }

                std::string tool_str;
                llvm::raw_string_ostream tool_output(tool_str);

                magnifier::ValueId function_id;
                try {
                    function_id = std::stoul(args[1], nullptr, 10);
                } catch (...) {
                    return "Invalid args";
                }

                if (data->explorer->PrintFunction(function_id, tool_output)) {
                    tool_output.flush();
                    return tool_str;
                } else {
                    return "Function not found: " + std::to_string(function_id) + "\n";
                }
            }},
            // Devirtualize function: `dc <instruction_id> <function_id>`
            {"dc", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
                static const std::unordered_map<magnifier::DevirtualizeError, std::string> devirtualize_error_map = {
                        {magnifier::DevirtualizeError::kNotACallBaseInstruction, "Not a CallBase instruction"},
                        {magnifier::DevirtualizeError::kInstructionNotFound,     "Instruction not found"},
                        {magnifier::DevirtualizeError::kFunctionNotFound,        "Function not found"},
                        {magnifier::DevirtualizeError::kNotAIndirectCall,        "Can only devirtualize indirect call"},
                        {magnifier::DevirtualizeError::kArgNumMismatch,          "Function takes a different number of parameter"}
                };

                if (args.size() != 3) {
                    return "Usage: dc <instruction_id> <function_id> - Devirtualize function\n";
                }

                std::string tool_str;
                llvm::raw_string_ostream tool_output(tool_str);
                SubstitutionObserver substitution_observer(tool_output);

                magnifier::ValueId instruction_id;
                magnifier::ValueId function_id;
                try {
                    instruction_id = std::stoul(args[1], nullptr, 10);
                    function_id = std::stoul(args[2], nullptr, 10);
                } catch (...) {
                    return "Invalid args";
                }


                magnifier::Result<magnifier::ValueId, magnifier::DevirtualizeError> result = data->explorer->DevirtualizeFunction(instruction_id, function_id, substitution_observer);


                if (result.Succeeded()) {
                    data->explorer->PrintFunction(result.Value(), tool_output);
                } else {
                    tool_output << "Devirtualize function call failed for id: " << instruction_id << " (error: " << devirtualize_error_map.at(result.Error()) << ")\n";
                }

                tool_output.flush();
                return tool_str;
            }},
            // Delete function: `df! <function_id>`
            {"df!", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
                static const std::unordered_map<magnifier::DeletionError, std::string> deletion_error_map = {
                        {magnifier::DeletionError::kIdNotFound, "Function id not found"},
                        {magnifier::DeletionError::kFunctionInUse, "Function is still in use"},
                };

                if (args.size() != 2) {
                    return "Usage: df! <function_id> - Delete function\n";
                }

                std::string tool_str;
                llvm::raw_string_ostream tool_output(tool_str);

                magnifier::ValueId function_id;
                try {
                    function_id = std::stoul(args[1], nullptr, 10);
                } catch (...) {
                    return "Invalid args";
                }
                std::optional<magnifier::DeletionError> result = data->explorer->DeleteFunction(function_id);
                if (!result) {
                    tool_output << "Deleted function with id: " << function_id << "\n";
                } else {
                    tool_output << "Delete function failed for id: " << function_id << " (error: " << deletion_error_map.at(result.value()) << ")\n";
                }

                tool_output.flush();
                return tool_str;
            }},
            // Inline function call: `ic <instruction_id>`
            {"ic", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
                static const std::unordered_map<magnifier::InlineError, std::string> inline_error_map = {
                        {magnifier::InlineError::kNotACallBaseInstruction, "Not a CallBase instruction"},
                        {magnifier::InlineError::kInstructionNotFound,     "Instruction not found"},
                        {magnifier::InlineError::kCannotResolveFunction,   "Cannot resolve function"},
                        {magnifier::InlineError::kInlineOperationFailed,   "Inline operation failed"},
                        {magnifier::InlineError::kVariadicFunction,        "Inlining variadic function is yet to be supported"},
                        {magnifier::InlineError::kResolveFunctionTypeMismatch, "Resolve function type mismatch"},
                };

                if (args.size() != 2) {
                    return "Usage: ic <instruction_id> - Inline function call\n";
                }

                std::string tool_str;
                llvm::raw_string_ostream tool_output(tool_str);
                FunctionResolver resolver{};
                SubstitutionObserver substitution_observer(tool_output);

                magnifier::ValueId instruction_id;
                try {
                    instruction_id = std::stoul(args[1], nullptr, 10);
                } catch (...) {
                    return "Invalid args";
                }

                magnifier::Result<magnifier::ValueId, magnifier::InlineError> result = data->explorer->InlineFunctionCall(instruction_id, resolver, substitution_observer);

                if (result.Succeeded()) {
                    data->explorer->PrintFunction(result.Value(), tool_output);
                } else {
                    tool_output << "Inline function call failed for id: " << instruction_id << " (error: " << inline_error_map.at(result.Error()) << ")\n";
                }

                tool_output.flush();
                return tool_str;
            }},
            // Substitute with value: `sv <id> <val>`
            {"sv", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
                static const std::unordered_map<magnifier::SubstitutionError, std::string> substitution_error_map = {
                        {magnifier::SubstitutionError::kIdNotFound,    "Instruction not found"},
                        {magnifier::SubstitutionError::kIncorrectType, "Instruction has non-integer type"},
                        {magnifier::SubstitutionError::kCannotUseFunctionId, "Expecting an instruction id instead of a function id"},
                };

                if (args.size() != 3) {
                    return "Usage: sv <id> <val> - Substitute with value\n";
                }

                std::string tool_str;
                llvm::raw_string_ostream tool_output(tool_str);
                SubstitutionObserver substitution_observer(tool_output);

                magnifier::ValueId value_id;
                uint64_t value;
                try {
                    value_id = std::stoul(args[1], nullptr, 10);
                    value = std::stoul(args[2], nullptr, 10);
                } catch (...) {
                   return "Invalid args";
                }

                // Try treating `value_id` as an instruction id
                magnifier::Result<magnifier::ValueId, magnifier::SubstitutionError> result = data->explorer->SubstituteInstructionWithValue(value_id, value, substitution_observer);

                if (result.Succeeded()) {
                    data->explorer->PrintFunction(result.Value(), tool_output);
                    tool_output.flush();
                    return tool_str;
                }

                if (result.Error() != magnifier::SubstitutionError::kIdNotFound) {
                    return "Substitute value failed for id:  " + std::to_string(value_id) + " (error: " + substitution_error_map.at(result.Error()) + ")\n";
                }

                // Try treating `value_id` as an argument id
                result = data->explorer->SubstituteArgumentWithValue(value_id, value, substitution_observer);

                if (result.Succeeded()) {
                    data->explorer->PrintFunction(result.Value(), tool_output);
                    tool_output.flush();
                    return tool_str;
                } else {
                    return "Substitute value failed for id:  " + std::to_string(value_id) + " (error: " + substitution_error_map.at(result.Error()) + ")\n";
                }
            }},
            // Optimize function bitcode using optimization level -O1: `o1 <id>`
            {"o1", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
                if (args.size() != 2) {
                    return "Usage: o1 <id> - Optimize function bitcode using optimization level -O1\n";
                }

                std::string tool_str;
                llvm::raw_string_ostream tool_output(tool_str);

                magnifier::ValueId function_id;
                try {
                    function_id = std::stoul(args[1], nullptr, 10);
                } catch (...) {
                    return "Invalid args";
                }
                RunOptimization(*data->explorer, tool_output, function_id, llvm::OptimizationLevel::O1);

                tool_output.flush();
                return tool_str;
            }},
            // Optimize function bitcode using optimization level -O2: `o2 <id>`
            {"o2", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
                if (args.size() != 2) {
                    return "Usage: o2 <id> - Optimize function bitcode using optimization level -O2\n";
                }

                std::string tool_str;
                llvm::raw_string_ostream tool_output(tool_str);

                magnifier::ValueId function_id;
                try {
                    function_id = std::stoul(args[1], nullptr, 10);
                } catch (...) {
                    return "Invalid args";
                }
                RunOptimization(*data->explorer, tool_output, function_id, llvm::OptimizationLevel::O2);

                tool_output.flush();
                return tool_str;
            }},
            // Optimize function bitcode using optimization level -O3: `o3 <id>`
            {"o3", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
                if (args.size() != 2) {
                    return "Usage: o3 <id> - Optimize function bitcode using optimization level -O3\n";
                }

                std::string tool_str;
                llvm::raw_string_ostream tool_output(tool_str);

                magnifier::ValueId function_id;
                try {
                    function_id = std::stoul(args[1], nullptr, 10);
                } catch (...) {
                    return "Invalid args";
                }
                RunOptimization(*data->explorer, tool_output, function_id, llvm::OptimizationLevel::O3);

                tool_output.flush();
                return tool_str;
            }},
            // Decompile function
            {"dec", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
                if (args.size() != 2) {
                    return "Usage: dec <id> - Decompile function with id\n";
                }
                magnifier::ValueId function_id;
                try {
                    function_id = std::stoul(args[1], nullptr, 10);
                } catch (...) {
                    return "Invalid args";
                }

                std::string ir_output_str;
                llvm::raw_string_ostream ir_output_stream(ir_output_str);
                std::string c_output_str;
                llvm::raw_string_ostream c_output_stream(c_output_str);

                magnifier::BitcodeExplorer &explorer = *data->explorer;
                AAW aaw(explorer);

                std::optional<llvm::Function *> target_function_opt = explorer.GetFunctionById(function_id);

                if (!target_function_opt) {
                    return "No function with id found";
                }

                std::unique_ptr<llvm::Module> module = llvm::CloneModule(*(*target_function_opt)->getParent());;

                llvm::Function *selected_function = nullptr;
                for (auto &function : module->functions()) {
                    if (explorer.GetId(function, magnifier::ValueIdKind::kDerived) == function_id) {
                        function.print(ir_output_stream, &aaw);
                        ir_output_stream.flush();
                        selected_function = &function;
                    }
                }
                assert(selected_function);

                rellic::Result<rellic::DecompilationResult, rellic::DecompilationError> r = rellic::Decompile(std::move(module));
                if (!r.Succeeded()) {
                    auto error = r.TakeError();
                    return error.message+"\n";
                }
                auto result = r.TakeValue();

                auto selected_function_decl = result.value_to_decl_map.at((llvm::Value *)selected_function);

                PrintDecl((clang::Decl *) selected_function_decl,
                          result.ast->getASTContext().getPrintingPolicy(), 0, c_output_stream);

                c_output_stream.flush();
                return llvm::json::Object{{
                                                  {"ir", ir_output_str},
                                                  {"code", c_output_str},
                                                  {"provenance", GetRellicProvenance(result)}
                                          }};
            }},
            {"upload", [](UserData *data, const llvm::json::Object &json, const std::vector<std::string> &args) -> llvm::json::Value {
                auto file_hex_str = json.getString("file");
                if (!file_hex_str) {
                    return "invalid upload file";
                }

                std::string file_str;
                bool r = llvm::tryGetFromHex(*file_hex_str, file_str);
                if (!r) {
                    return "invalid upload file";
                }

                std::unique_ptr<llvm::Module> mod{rellic::LoadModuleFromMemory(&(*data->llvm_context), file_str, true)};
                if (!mod) {
                    return "invalid upload file";
                }

                // Only support having one module at a time due to design limitations
//                data->explorer = std::make_unique<magnifier::BitcodeExplorer>(*data->llvm_context);

                data->explorer->TakeModule(std::move(mod));
                return "module uploaded";
            }}
    };


    auto cmd_str = json.getString("cmd");
    auto packet_id = json.getInteger("id");
    if (!cmd_str || !packet_id) {
        return llvm::json::Object {
                {"message","required fields not found"}};
    }

    auto tokenized_input = split(cmd_str->str(), ' ');
    if (tokenized_input.empty()) {
        return llvm::json::Object {
                {"cmd", cmd_str},
                {"id", packet_id},
                {"output","Invalid Command\n"}};
    }

    auto cmd = cmd_map.find(tokenized_input[0]);
    if (cmd == cmd_map.end()) {
        return llvm::json::Object {
                {"cmd", cmd_str},
                {"id", packet_id},
                {"output","Invalid Command: " + tokenized_input[0] + "\n"}};
    }

    return llvm::json::Object {
            {"cmd", cmd_str},
            {"id", packet_id},
            {"output",cmd->second(data, json, tokenized_input)}
    };

}

us_listen_socket_t *listen_socket = nullptr;

int main(int argc, char **argv) {
    llvm::InitLLVM x(argc, argv);


    uWS::App().ws<UserData>("/ws", {
        .maxPayloadLength = 50 * 1024 * 1024,
        .open = [](auto *ws){
            UserData *data = ws->getUserData();
            data->llvm_context = std::make_unique<llvm::LLVMContext>();
            data->explorer = std::make_unique<magnifier::BitcodeExplorer>(*data->llvm_context);
        },

        .message = [](auto *ws, std::string_view message, uWS::OpCode opCode) {
            // Only deal with text
            if (opCode != uWS::TEXT) {return;}


            auto json{llvm::json::parse(message)};
            if (!json || json->kind() != llvm::json::Value::Object) {
                ws->send(JsonToString(llvm::json::Object {
                    {"message","Invalid JSON message"}
                }), opCode);
                return;
            }

            if (json->getAsObject()->getString("cmd")->str() == "exit") {
                std::cout << "exiting" << std::endl;
                if (listen_socket) {
                    us_listen_socket_close(0, listen_socket);
                    listen_socket = nullptr;
                }
                return;
            }

            ws->send(JsonToString(HandleRequest(ws->getUserData(), *json->getAsObject())), opCode);
        }
    }).listen(9001, [](auto *socket) {
        if (socket) {
            std::cout << "Listening on port " << 9001 << std::endl;
            listen_socket = socket;
        }
    }).run();
    return 0;
}

