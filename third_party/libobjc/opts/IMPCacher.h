#include "LLVMCompat.h"
#include "llvm/Support/CallSite.h"
namespace llvm
{
  class BasicBlock;
  class CallInst;
  class Function;
  class Instruction;
  class IntegerType;
  class LLVMContext;
  class MDNode;
  class Pass;
  class PointerType;
  class Value;
}

using namespace llvm;

namespace GNUstep
{
  class IMPCacher
  {
    private:
      LLVMContext &Context;
      MDNode *AlreadyCachedFlag;
      unsigned IMPCacheFlagKind;
      Pass *Owner;
      LLVMPointerType *PtrTy;
      LLVMPointerType *IdTy;
      LLVMIntegerType *IntTy;
    public:
      IMPCacher(LLVMContext &C, Pass *owner);
      void CacheLookup(Instruction *lookup, Value *slot, Value *version, bool
          isSuperMessage=false);
      void SpeculativelyInline(Instruction *call, Function *function);
      /**
       * Turns a call to objc_msgSend*() into a call to
       * objc_msg_lookup_sender() and a call to the resulting IMP.  The call to
       * the IMP is returned.  The single call is faster, but prevents caching.
       * The split call allows caching, which is faster in the best case and
       * slower in the worst...
       */
      CallSite SplitSend(CallSite msgSend);
  };

  void removeTerminator(BasicBlock *BB);
  void addPredecssor(BasicBlock *block, BasicBlock *oldPredecessor, BasicBlock
          *newPredecessor);
}
