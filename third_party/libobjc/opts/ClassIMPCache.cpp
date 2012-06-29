#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/LLVMContext.h"
#include "llvm/Instructions.h"
#include "llvm/Constants.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/DefaultPasses.h"
#include "ObjectiveCOpts.h"
#include "IMPCacher.h"
#include <string>
#include "LLVMCompat.h"

using namespace GNUstep;
using namespace llvm;
using std::string;

namespace 
{
  class ClassIMPCachePass : public ModulePass 
  {
    LLVMIntegerType *IntTy;

    public:
    static char ID;
    ClassIMPCachePass() : ModulePass(ID) {}

    virtual bool runOnModule(Module &M) {
      Function *sendFn = M.getFunction("objc_msgSend");
      Function *send_stretFn = M.getFunction("objc_msgSend_stret");
      Function *send_fpretFn = M.getFunction("objc_msgSend_fpret");
      Function *lookupFn =M.getFunction("objc_msg_lookup_sender");
      // If this module doesn't contain any message sends, then skip it
      if ((sendFn == 0) && (send_stretFn == 0) && (send_fpretFn == 0) &&
          (lookupFn ==0)) { return false; }

      GNUstep::IMPCacher cacher = GNUstep::IMPCacher(M.getContext(), this);
      IntTy = (sizeof(int) == 4 ) ? Type::getInt32Ty(M.getContext()) :
          Type::getInt64Ty(M.getContext()) ;
      bool modified = false;

      unsigned MessageSendMDKind = M.getContext().getMDKindID("GNUObjCMessageSend");

      for (Module::iterator F=M.begin(), fend=M.end() ;
          F != fend ; ++F) {

        if (F->isDeclaration()) { continue; }

        SmallVector<std::pair<CallSite, bool>, 16> Lookups;
        SmallVector<CallSite, 16> Sends;

        for (Function::iterator i=F->begin(), end=F->end() ;
            i != end ; ++i) {
          for (BasicBlock::iterator b=i->begin(), last=i->end() ;
              b != last ; ++b) {
            CallSite call(b);
            if (call.getInstruction()) {
              Value *callee = call.getCalledValue()->stripPointerCasts();
              if (Function *func = dyn_cast<Function>(callee)) {
                if ((func == lookupFn) || (func == sendFn) ||
                    (func == send_fpretFn) || (func == send_stretFn)) {
                  MDNode *messageType = 
                    call.getInstruction()->getMetadata(MessageSendMDKind);
                  if (0 == messageType) { continue; }
                  if (cast<ConstantInt>(messageType->getOperand(2))->isOne()) {
                    if (func == lookupFn) {
                      Lookups.push_back(std::pair<CallSite, bool>(call, false));
                    } else {
                      Sends.push_back(call);
                    }
                  }
                } else if (func->getName() == "objc_slot_lookup_super") {
                  Lookups.push_back(std::pair<CallSite, bool>(call, true));
                }
              }
            }
          }
        }
        for (SmallVectorImpl<CallSite>::iterator i=Sends.begin(), 
            e=Sends.end() ; e!=i ; i++) {
          Lookups.push_back(std::pair<CallSite, bool>(cacher.SplitSend(*i), false));
        }
        for (SmallVectorImpl<std::pair<CallSite, bool> >::iterator
            i=Lookups.begin(), e=Lookups.end() ; e!=i ; i++) {
          Instruction *call = i->first.getInstruction();
          LLVMType *SlotPtrTy = call->getType();

          Value *slot = new GlobalVariable(M, SlotPtrTy, false,
              GlobalValue::PrivateLinkage, Constant::getNullValue(SlotPtrTy),
              "slot");
          Value *version = new GlobalVariable(M, IntTy, false,
              GlobalValue::PrivateLinkage, Constant::getNullValue(IntTy),
              "version");
          cacher.CacheLookup(call, slot, version, i->second);
        }
      }
      return modified;
    }
  };

  char ClassIMPCachePass::ID = 0;
  RegisterPass<ClassIMPCachePass> X("gnu-class-imp-cache", 
          "Cache IMPs for class messages");
}

ModulePass *createClassIMPCachePass(void)
{
  return new ClassIMPCachePass();
}
