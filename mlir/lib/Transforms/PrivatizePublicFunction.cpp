//===- PrivatizePublicFunction.cpp - Remove Dead Values --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "mlir/Analysis/DataFlow/DeadCodeAnalysis.h"
#include "mlir/Analysis/DataFlow/LivenessAnalysis.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Value.h"
#include "mlir/IR/ValueRange.h"
#include "mlir/IR/Visitors.h"
#include "mlir/Interfaces/CallInterfaces.h"
#include "mlir/Interfaces/ControlFlowInterfaces.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Transforms/FoldUtils.h"
#include "mlir/Transforms/Passes.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/DebugLog.h"
#include <cassert>
#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#define DEBUG_TYPE "privatize-public-function"

namespace mlir {
#define GEN_PASS_DEF_PRIVATIZEPUBLICFUNCTION
#include "mlir/Transforms/Passes.h.inc"
} // namespace mlir

using namespace mlir;
using namespace mlir::dataflow;

//===----------------------------------------------------------------------===//
// PrivatizePublicFunction Pass
//===----------------------------------------------------------------------===//
namespace {

struct PrivatizePublicFunction
    : public impl::PrivatizePublicFunctionBase<PrivatizePublicFunction> {
  void runOnOperation() override;
};

} // namespace

static BitVector markLives(ArrayRef<Value> values, RunLivenessAnalysis &la) {
  BitVector lives(values.size(), true);

  for (auto [index, value] : llvm::enumerate(values)) {
    const Liveness *liveness = la.getLiveness(value);
    if (!liveness) {
      LDBG() << "No liveness analysis available for value " << value
             << " at index " << index;
      continue;
    }

    if (!liveness->isLive) {
      lives.reset(index);
      LDBG() << "Value " << value << " at index " << index
             << " is dead according to liveness analysis";
    } else {
      LDBG() << "Value " << value << " at index " << index
             << " is live according to liveness analysis";
    }
  }

  return lives;
}

static void processCallOp(CallOpInterface callOp, Operation *module,
                         RunLivenessAnalysis &la) {
  Operation *callableOp = callOp.resolveCallable();
  auto funcOp = dyn_cast<FunctionOpInterface>(callableOp);
  if (!funcOp || !funcOp.isPublic()) {
    return;
  }

  LDBG() << "Processing callOp " << callOp
        << " target is public function: " << funcOp.getOperation()->getName();

  // Get the list of unnecessary (non-live) arguments in `nonLiveArgs`.
  SmallVector<Value> arguments(callOp.getArgOperands());
  BitVector nonLiveArgs = markLives(arguments, la);
  nonLiveArgs = nonLiveArgs.flip();

  if (nonLiveArgs.count() > 0) {
    auto moduleOp = cast<ModuleOp>(module);
    OpBuilder rewriter(moduleOp.getContext());

    // Clone function and migrate function body to the new private function
    FunctionOpInterface clonedFunc = cast<FunctionOpInterface>(funcOp.clone());

    // Set visibility = 'private' and a new name for the cloned function
    SymbolTable::setSymbolVisibility(clonedFunc,
                                  SymbolTable::Visibility::Private);
    std::string newName = "__" + funcOp.getName().str() + "_privatized";
    clonedFunc.setName(newName);

    // Insert the cloned function into the module
    rewriter.setInsertionPointAfter(funcOp);
    rewriter.insert(clonedFunc);

    // Replace ALL callsites of the original function to call the cloned function directly
    LogicalResult result = SymbolTable::replaceAllSymbolUses(
        funcOp,
        clonedFunc.getNameAttr(),
        moduleOp
    );

    if (result.failed()) {
      LDBG() << "Failed to replace all symbol uses for " << funcOp.getName();
      return;
    }

    LDBG() << "Redirected all callsites from " << funcOp.getName()
           << " to " << newName;

    // Transform the original funcOp into a wrapper that calls the cloned function
    // (This preserves the public API in case there are external references)
    Region &funcBody = funcOp.getFunctionBody();

    // Clean the original function body
    funcBody.dropAllReferences();
    funcBody.getBlocks().clear();

    // Create a new entry block for the wrapper function
    Block *wrapperBlock = rewriter.createBlock(&funcBody);

    // Add block arguments that match the function signature (take all arguments)
    for (Type argType : funcOp.getArgumentTypes()) {
      wrapperBlock->addArgument(argType, funcOp.getLoc());
    }

    // Set insertion point to the new block
    rewriter.setInsertionPointToStart(wrapperBlock);

    // Create a call to the cloned private function with all arguments
    SmallVector<Value> callArgs(wrapperBlock->getArguments());
    auto newCallOp = rewriter.create<func::CallOp>(
        funcOp.getLoc(),
        funcOp.getResultTypes(),
        newName,
        callArgs
    );

    // Return all return values of the new call
    rewriter.create<func::ReturnOp>(funcOp.getLoc(), newCallOp.getResults());

    LDBG() << "Created wrapper function " << funcOp.getName()
           << " that calls " << newName;
  }
}


void PrivatizePublicFunction::runOnOperation() {
  auto &la = getAnalysis<RunLivenessAnalysis>();
  Operation *module = getOperation();

  // if liveness analysis is not interprocedural, do nothing.
  if (!la.getSolverConfig().isInterprocedural())  {
    return;
  }

  module->walk([&](CallOpInterface callOp) {
    processCallOp(callOp, module, la);
  });
}

std::unique_ptr<Pass> mlir::createPrivatizePublicFunctionPass() {
  return std::make_unique<PrivatizePublicFunction>();
}
