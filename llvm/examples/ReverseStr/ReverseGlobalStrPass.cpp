#include "llvm/IR/Module.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Pass.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ConstantFolding.h"

#define DEBUG_TYPE "ReverseStr"
using namespace llvm;

namespace {

bool reverse_global_str(Module &M) {
  LLVMContext &ctx = M.getContext();

  for (auto &g: M.globals()) {
    if (g.hasName() && g.hasInitializer()) {
      LLVM_DEBUG(dbgs() << "global variable: " << g << '\n');

      const Constant *c = g.getInitializer();
      Type *ty = c->getType();
      if(ty->isArrayTy() && cast<ArrayType>(ty)->getElementType()->isIntegerTy(8)/*i8*/) {
        LLVM_DEBUG(dbgs() << "initializer: "  << c << '\n');

        Constant *contents= llvm::ReadByteArrayFromGlobal(&g, 0);
        if (contents != nullptr) {
          ConstantDataSequential *cds = cast<ConstantDataSequential>(contents);
          if (cds->isCString()) {
            StringRef literal = cds->getAsCString();
            LLVM_DEBUG(dbgs() << "original c-string literal : " << literal << '\n');

            if (!literal.empty()) {
              size_t len = literal.size();
              SmallVector<unsigned char, 256> RawBytes(len + 1);
              for (size_t i=0; i < len; ++i) {
                RawBytes[i] = literal[len - 1  - i];
              }
              RawBytes[len] = '\0';

              auto reversed = ConstantDataArray::get(ctx, RawBytes);

              g.setInitializer(reversed);
              LLVM_DEBUG(dbgs() <<  "after updated GV: " << g << '\n');
            } else {
              LLVM_DEBUG(dbgs() << "[empty] skipped!");
            }
          }
        }
      }
    }
  }
  return true;
}


struct LegacyReverseStr : public ModulePass {
  static char ID;
  LegacyReverseStr() : ModulePass(ID) {}
  bool runOnFunction(Module &M) override { return reverse_global_str(M); }
};

struct ReverseGlobalStrPass: PassInfoMixin<ReverseGlobalStrPass> {
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &) {
    reverse_global_str(M);
    return PreservedAnalyses::none();
  }

  static bool isRequired() { return true; }
};

} // namespace

char LegacyReverseStr::ID = 0;

static RegisterPass<LegacyReverseStr> X("ReverseStr", "Reverse global variables of c-str"
                                 false /* Only looks at CFG */,
                                 false /* Analysis Pass */);

/* New PM Registration */
llvm::PassPluginLibraryInfo getReverseStrPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, "ReverseStr", LLVM_VERSION_STRING,
          [](PassBuilder &PB) {
            PB.registerPipelineStartEPCallback(
                [](llvm::ModulePassManager &PM, OptimizationLevel Level) {
                  PM.addPass(ReverseGlobalStrPass());
                });
            PB.registerPipelineParsingCallback(
                [](StringRef Name, llvm::ModulePassManager &PM,
                   ArrayRef<llvm::PassBuilder::PipelineElement>) {
                  if (Name == "ReverseStr") {
                    PM.addPass(ReverseGlobalStrPass());
                    return true;
                  }
                  return false;
                });
          }};
}

extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
llvmGetPassPluginInfo() {
  return getReverseStrPluginInfo();
}