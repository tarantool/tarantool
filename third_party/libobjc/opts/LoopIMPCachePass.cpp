#include "llvm/LLVMContext.h"
#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/Instructions.h"
#include "llvm/Constants.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/DefaultPasses.h"
#include "ObjectiveCOpts.h"
#include "IMPCacher.h"
#include <string>

using namespace GNUstep;
using namespace llvm;
using std::string;

namespace 
{
  class GNULoopIMPCachePass : public FunctionPass 
  {
    GNUstep::IMPCacher *cacher;
    LLVMIntegerType *IntTy;
    Module *M;
    bool skip;
    Function *sendFn;
    Function *lookupFn;
    Function *send_stretFn;
    Function *send_fpretFn;

    public:
    static char ID;
    GNULoopIMPCachePass() : FunctionPass(ID) {}
    ~GNULoopIMPCachePass() { delete cacher; }

    virtual bool doInitialization(Module &Mod) {
      cacher = new GNUstep::IMPCacher(Mod.getContext(), this);
      IntTy = (sizeof(int) == 4 ) ? Type::getInt32Ty(Mod.getContext()) :
          Type::getInt64Ty(Mod.getContext()) ;
      M = &Mod;
      skip = false;
      sendFn = M->getFunction("objc_msgSend");
      send_stretFn = M->getFunction("objc_msgSend_stret");
      send_fpretFn = M->getFunction("objc_msgSend_fpret");
      lookupFn =M->getFunction("objc_msg_lookup_sender");
      // If this module doesn't contain any message sends, then skip it
      if ((sendFn == 0) && (send_stretFn == 0) && (send_fpretFn == 0) &&
          (lookupFn ==0)) {
        skip = true;
      }
      return false;  
    }

    virtual void getAnalysisUsage(AnalysisUsage &Info) const {
      Info.addRequired<LoopInfo>();
    }


    virtual bool runOnFunction(Function &F) {
      if (skip) { return false; }
      LoopInfo &LI = getAnalysis<LoopInfo>();
      bool modified = false;
      SmallVector<CallSite, 16> Lookups;
      SmallVector<CallSite, 16> Sends;
      BasicBlock *entry = &F.getEntryBlock();

      for (Function::iterator i=F.begin(), end=F.end() ;
          i != end ; ++i) {
        // Ignore basic blocks that are not parts of loops.
        if (LI.getLoopDepth(i) == 0) { continue; }
        for (BasicBlock::iterator b=i->begin(), last=i->end() ;
            b != last ; ++b) {
          CallSite call = CallSite(b);
          if (CallSite() != call) {
            Value *callee = call.getCalledValue()->stripPointerCasts();
            Function *func = dyn_cast<Function>(callee);
            if (func) {
              if (func == lookupFn) {
                modified = true;
                Lookups.push_back(call);
              } else if ((func == sendFn) || (func == send_fpretFn) ||
                         (func == send_stretFn)) {
                modified = true;
                Sends.push_back(call);
              }
            }
          }
        }
      }
      for (SmallVectorImpl<CallSite>::iterator i=Sends.begin(), 
          e=Sends.end() ; e!=i ; i++) {
        Lookups.push_back(cacher->SplitSend(*i));
      }
      IRBuilder<> B = IRBuilder<>(entry);
      for (SmallVectorImpl<CallSite>::iterator i=Lookups.begin(), 
          e=Lookups.end() ; e!=i ; i++) {
        LLVMType *SlotPtrTy = (*i)->getType();
        B.SetInsertPoint(entry, entry->begin());
        Value *slot = B.CreateAlloca(SlotPtrTy, 0, "slot");
        Value *version = B.CreateAlloca(IntTy, 0, "slot_version");

        B.CreateStore(Constant::getNullValue(SlotPtrTy), slot);
        B.CreateStore(Constant::getNullValue(IntTy), version);
        cacher->CacheLookup(i->getInstruction(), slot, version);
      }
#ifdef DEBUG
      if (modified){
        verifyFunction(F);
      }
#endif
      return modified;
    }
  };

  char GNULoopIMPCachePass::ID = 0;
  RegisterPass<GNULoopIMPCachePass> X("gnu-loop-imp-cache", 
          "Cache IMPs in loops pass");
}

FunctionPass *createGNULoopIMPCachePass(void)
{
  return new GNULoopIMPCachePass();
}
