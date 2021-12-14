#include "llvm/Support/InitLLVM.h"
#include "BitcodeExplorer.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <map>
#include <functional>

std::vector<std::string> split(const std::string& input, char delimiter) {
    std::stringstream ss(input);
    std::vector<std::string> results;
    std::string token;
    while (std::getline(ss, token, delimiter))
        results.push_back(token);
    return results;
}

int main(int argc, char** argv){
    llvm::InitLLVM X(argc, argv);

    BitcodeExplorer explorer;

    std::map<std::string, std::function<void(const std::vector<std::string>&)>> cmd_map = {
            {"lm", [&explorer](const std::vector<std::string>& args) -> void {
                if (args.size() != 2) {
                    std::cout << "Usage: lm <path> - Load/open an LLVM .bc or .ll module" << std::endl;
                    return;
                }
                explorer.OpenFile(args[1]);
            }},
            {"lf", [&explorer](const std::vector<std::string>& args) -> void {
                if (args.size() != 1) {
                    std::cout << "Usage: lf - List all functions in all open modules" << std::endl;
                    return;
                }
                explorer.ListFunctions();
            }},
            {"pf", [&explorer](const std::vector<std::string>& args) -> void {
                if (args.size() != 2) {
                    std::cout << "Usage: pf <function_id> - Print function" << std::endl;
                    return;
                }
                explorer.PrintFunction(std::stoul(args[1], nullptr, 10));

            }},
            {"hello", [](const std::vector<std::string>& args) {

                std::cout << "hello world command" << std::endl;
                for (const std::string& val : args)
                    std::cout << val << std::endl;
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

        std::map<std::string, std::function<void(const std::vector<std::string>&)>>::iterator cmd = cmd_map.find(tokenized_input[0]);
        if (cmd != cmd_map.end()) cmd->second(tokenized_input);
        else std::cout << "Invalid Command: " << tokenized_input[0] << std::endl;


    }
    return 0;
}