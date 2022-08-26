/*
 * Copyright (c) 2021-present, Trail of Bits, Inc.
 * All rights reserved.
 *
 * This source code is licensed in accordance with the terms specified in
 * the LICENSE file found in the root directory of this source tree.
 */

#pragma once

#include <llvm/Passes/PassBuilder.h>
#include <magnifier/ISubstitutionObserver.h>
#include <magnifier/Result.h>

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace llvm {
class AssemblyAnnotationWriter;
class Function;
class Instruction;
class LLVMContext;
class Module;
class raw_ostream;
class WeakVH;
class Value;
class Use;
class FunctionCallee;
class Type;
class BasicBlock;
class CallInst;
}  // namespace llvm

namespace magnifier {
class IdCommentWriter;
class IFunctionResolver;

using ValueId = uint64_t;
static constexpr ValueId kInvalidValueId = 0;
enum class ValueIdKind {
  kOriginal,      // Corresponds with `!explorer.source_id`.
  kDerived,       // Corresponds with `!explorer.id`.
  kBlock,         // Corresponds with `!explorer.block_id`.
  kSubstitution,  // Corresponds with `!explorer.substitution_kind_id`.
};

enum class FunctionKind {
  kOriginal,   // Functions directly loaded from llvm bitcode files
  kGenerated,  // Functions generated after an operation (inlining, etc)
};

enum class InlineError {
  kNotACallBaseInstruction,  // Not a CallBase instruction
  kInstructionNotFound,      // Instruction not found
  kCannotResolveFunction,    // Cannot resolve function
  kInlineOperationFailed,    // Inline operation failed
  kVariadicFunction,  // Inlining variadic function is yet to be supported
  kResolveFunctionTypeMismatch,  // Resolve function type mismatch
};

enum class SubstitutionError {
  kIdNotFound,           // ValueId not found
  kIncorrectType,        // Instruction is not of the desired type
  kCannotUseFunctionId,  // Expecting an instruction id instead of a function id
};

enum class OptimizationError {
  kInvalidOptimizationLevel,  // The provided optimization level is not allowed
  kIdNotFound,                // Function id not found
};

enum class DeletionError {
  kIdNotFound,     // Function id not found
  kFunctionInUse,  // Function is still in use
};

enum class DevirtualizeError {
  kInstructionNotFound,      // Instruction not found
  kNotACallBaseInstruction,  // Not a CallBase instruction
  kFunctionNotFound,         // Function not found
  kNotAIndirectCall,         // Instruction do not refer to an indirect call
  kArgNumMismatch,           // Function takes a different number of parameter
};

class BitcodeExplorer {
 private:
  // Reference to the llvm context
  llvm::LLVMContext &llvm_context;
  // ID of the `!explorer.id` metadata. This metadata holds the unique ID of
  // this value.
  const unsigned md_explorer_id;
  // ID of the `!explorer.source_id` metadata. This metadata tracks the
  // provenance of a value. If a value, e.g. an instruction, is from a function,
  // then usually the `!explorer.id` and `!explorer.source_id` match. However,
  // if the value has been subject to mutation or inlining, then the
  // `!explorer.source_id` will stay constant while the `!explorer.id` will
  // change.
  const unsigned md_explorer_source_id;
  // ID of the `!explorer.block_id` metadata. This metadata should only be
  // attached to terminator instructions of basic blocks. It serves a similar
  // purpose as `!explorer.id` but for uniquely identifying `BasicBlock` values.
  const unsigned md_explorer_block_id;
  // ID of the `!explorer.substitution_kind_id` metadata. This metadata should
  // only be attached to instructions that are going to be substituted by
  // `ElideSubstitutionHooks`. It helps determine the `SubstitutionKind` of that
  // instruction. It's most commonly applied to `CallInst` values calling the
  // substitute hook function.
  const unsigned md_explorer_substitution_kind_id;
  // Annotator object used for annotating function disassembly. It prints the
  // various metadata attached to each value.
  std::unique_ptr<llvm::AssemblyAnnotationWriter> annotator;
  // This is a vector of all the llvm `Module` objects ingested using
  // `TakeModule`.
  std::vector<std::unique_ptr<llvm::Module>> opened_modules;
  // A map between unique `ValueId`s and their corresponding functions.
  std::map<ValueId, llvm::WeakVH> function_map;
  // A map between unique `ValueId`s and their corresponding instructions.
  std::map<ValueId, llvm::WeakVH> instruction_map;
  // A map between unique `ValueId`s and their corresponding basic blocks.
  std::map<ValueId, llvm::WeakVH> block_map;
  // A map between unique `ValueId`s and their corresponding function arguments.
  std::map<ValueId, llvm::WeakVH> argument_map;
  // An increment only counter used for assigning unique ids to values.
  ValueId value_id_counter;
  // A temporary map between types and their corresponding substitute hook
  // functions. This map helps keep track of hooks that needs to be elided
  // during an operation. It should be cleared at the end of any high-level
  // operation.
  std::map<llvm::Type *, llvm::FunctionCallee> hook_functions;

  // Elide all substitute hooks present in `function`. Uses
  // `substitute_value_func` to guide the substitution process
  void ElideSubstitutionHooks(llvm::Function &function,
                              ISubstitutionObserver &substitution_observer);

  // Convert from opaque type `ValueIdKind` to actual llvm `kind_id`.
  [[nodiscard]] unsigned ValueIdKindToKindId(ValueIdKind kind) const;

  // Get a `FunctionCallee` object for the given `type`. Create the function in
  // `func_module` if it doesn't exist. In addition, the object is added to the
  // `hook_functions` map.
  llvm::FunctionCallee GetHookFunction(llvm::Type *type,
                                       llvm::Module *func_module);

  // Create and return a `CallInst` for calling the substitution hook.
  // It also attaches the correct `SubstitutionKind` metadata to the
  // instruction.
  llvm::CallInst *CreateHookCallInst(llvm::Type *type,
                                     llvm::Module *func_module,
                                     SubstitutionKind hook_kind,
                                     llvm::Value *old_val,
                                     llvm::Value *new_val);

  // Update/index a function by adding various metadata to function,
  // instruction, and block values. Also update `function_map`,
  // `instruction_map`, and `block_map` to reflect the changes.
  void UpdateMetadata(llvm::Function &function);

 public:
  explicit BitcodeExplorer(llvm::LLVMContext &llvm_context);

  BitcodeExplorer(BitcodeExplorer &&) = default;

  ~BitcodeExplorer();

  // Ingest `module` and take ownership.
  // It updates `opened_modules` and indexes all the functions inside the
  // module.
  void TakeModule(std::unique_ptr<llvm::Module> module);

  // Invoke `callback` on every indexed function while providing its `ValueID`
  // and `FunctionKind`.
  void ForEachFunction(const std::function<void(ValueId, llvm::Function &,
                                                FunctionKind)> &callback);

  // Given the function id, print function disassembly to `output_stream`
  bool PrintFunction(ValueId function_id, llvm::raw_ostream &output_stream);

  // Inline a call instruction
  Result<ValueId, InlineError> InlineFunctionCall(
      ValueId instruction_id, IFunctionResolver &resolver,
      ISubstitutionObserver &substitution_observer);

  // Substitute an instruction with integer value
  Result<ValueId, SubstitutionError> SubstituteInstructionWithValue(
      ValueId instruction_id, uint64_t value, ISubstitutionObserver &observer);

  // Substitute an argument with integer value
  Result<ValueId, SubstitutionError> SubstituteArgumentWithValue(
      ValueId argument_id, uint64_t value, ISubstitutionObserver &observer);

  // Optimize a function using a certain `optimization_level`
  Result<ValueId, OptimizationError> OptimizeFunction(
      ValueId function_id,
      const llvm::OptimizationLevel &optimization_level);

  // Delete a function that is not in use
  std::optional<DeletionError> DeleteFunction(ValueId function_id);

  // Devirtualize an indirect function call into a direct one
  Result<ValueId, DevirtualizeError> DevirtualizeFunction(
      ValueId instruction_id, ValueId function_id,
      ISubstitutionObserver &substitution_observer);

  // Returns the value ID for `function`, or `kInvalidValueId` if no ID is
  // found.
  [[nodiscard]] ValueId GetId(const llvm::Function &function,
                              ValueIdKind kind) const;

  // Returns the value ID for `instruction`, or `kInvalidValueId` if no ID is
  // found.
  [[nodiscard]] ValueId GetId(const llvm::Instruction &instruction,
                              ValueIdKind kind) const;

  // Set an id inside the metadata of a function.
  void SetId(llvm::Function &function, ValueId value, ValueIdKind kind) const;

  // Set an id inside the metadata of an instruction.
  void SetId(llvm::Instruction &instruction, ValueId value,
             ValueIdKind kind) const;

  // Remove id of `kind` from `function` metadata.
  void RemoveId(llvm::Function &function, ValueIdKind kind);

  // Remove id of `kind` from `instruction` metadata.
  void RemoveId(llvm::Instruction &instruction, ValueIdKind kind);

  ValueId MaxCurrentID();

  std::optional<llvm::Function *> GetFunctionById(ValueId);

  // Either gets the current id for a function or indexes the function.
  ValueId IndexFunction(llvm::Function &function);
};
}  // namespace magnifier
