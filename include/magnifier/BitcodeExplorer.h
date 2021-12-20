/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */


#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace llvm {
class AssemblyAnnotationWriter;
class Function;
class Instruction;
class LLVMContext;
class Module;
class raw_ostream;
class WeakVH;
}  // namespace llvm

namespace magnifier {
class IdCommentWriter;

using ValueId = uint64_t;

class BitcodeExplorer {

private:

    llvm::LLVMContext &llvm_context;
    const unsigned md_explorer_id;
    const unsigned md_explorer_source_id;
    const std::unique_ptr<llvm::AssemblyAnnotationWriter> annotator;
    std::vector<std::unique_ptr<llvm::Module>> opened_modules;
    std::map<ValueId, llvm::WeakVH> function_map;
    std::map<ValueId, llvm::WeakVH> instruction_map;
    ValueId value_id_counter;

    llvm::Function *WithClonedFunction(llvm::Function *function, const std::function<void(llvm::Function *)> &callback);

    [[nodiscard]] static ValueId GetIntMetadata(const llvm::Function &function, unsigned kind_id) ;

    [[nodiscard]] static ValueId GetIntMetadata(const llvm::Instruction &instruction, unsigned kind_id) ;

    void SetIntMetadata(llvm::Function &function, ValueId value, unsigned kind_id) const;

    void SetIntMetadata(llvm::Instruction &instruction, ValueId value, unsigned kind_id) const;

    void UpdateMetadata(llvm::Function &function);

public:
    explicit BitcodeExplorer(llvm::LLVMContext &llvm_context);

    ~BitcodeExplorer();

    void TakeModule(std::unique_ptr<llvm::Module> module);

    void ForEachFunction(const std::function<void(ValueId, llvm::Function & )> &callback, bool include_generated = false);

    bool PrintFunction(ValueId function_id, llvm::raw_ostream &output_stream);

    ValueId InlineFunctionCall(ValueId instruction_id, const std::function<void(llvm::Function *)> &resolve_declaration);

    [[nodiscard]] ValueId GetExplorerId(const llvm::Function &function) const;

    [[nodiscard]] ValueId GetExplorerId(const llvm::Instruction &instruction) const;

    void SetExplorerId(llvm::Function &function, ValueId value) const;

    void SetExplorerId(llvm::Instruction &instruction, ValueId value) const;

    [[nodiscard]] ValueId GetSourceId(const llvm::Function &function) const;

    [[nodiscard]] ValueId GetSourceId(const llvm::Instruction &instruction) const;

    void SetSourceId(llvm::Function &function, ValueId value) const;

    void SetSourceId(llvm::Instruction &instruction, ValueId value) const;
};
}