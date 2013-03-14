#include "llvm/Analysis/Verifier.h"
#include "IMPCacher.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include "LLVMCompat.h"

GNUstep::IMPCacher::IMPCacher(LLVMContext &C, Pass *owner) : Context(C),
  Owner(owner) {

  PtrTy = Type::getInt8PtrTy(Context);
  IntTy = (sizeof(int) == 4 ) ? Type::getInt32Ty(C) : Type::getInt64Ty(C);
  IdTy = PointerType::getUnqual(PtrTy);
  Value *AlreadyCachedFlagValue = MDString::get(C, "IMPCached");
  AlreadyCachedFlag = CreateMDNode(C, &AlreadyCachedFlagValue);
  IMPCacheFlagKind = Context.getMDKindID("IMPCache");
}

void GNUstep::IMPCacher::CacheLookup(Instruction *lookup, Value *slot, Value
    *version, bool isSuperMessage) {

  // If this IMP is already cached, don't cache it again.
  if (lookup->getMetadata(IMPCacheFlagKind)) { return; }

  lookup->setMetadata(IMPCacheFlagKind, AlreadyCachedFlag);
  bool isInvoke = false;

  BasicBlock *beforeLookupBB = lookup->getParent();
  BasicBlock *lookupBB = SplitBlock(beforeLookupBB, lookup, Owner);
  BasicBlock *lookupFinishedBB = lookupBB;
  BasicBlock *afterLookupBB;

  if (InvokeInst *inv = dyn_cast<InvokeInst>(lookup)) {
    afterLookupBB = inv->getNormalDest();
    lookupFinishedBB =
      BasicBlock::Create(Context, "done_lookup", lookupBB->getParent()); 
    CGBuilder B(lookupFinishedBB);
    B.CreateBr(afterLookupBB);
    inv->setNormalDest(lookupFinishedBB);
    isInvoke = true;
  } else {
    BasicBlock::iterator iter = lookup;
    iter++;
    afterLookupBB = SplitBlock(iter->getParent(), iter, Owner);
  }

  removeTerminator(beforeLookupBB);

  CGBuilder B = CGBuilder(beforeLookupBB);
  // Load the slot and check that neither it nor the version is 0.
  Value *versionValue = B.CreateLoad(version);
  Value *receiverPtr = lookup->getOperand(0);
  Value *receiver = receiverPtr;
  if (!isSuperMessage) {
    receiver = B.CreateLoad(receiverPtr);
  }
  // For small objects, we skip the cache entirely.  
  // FIXME: Class messages are never to small objects...
  bool is64Bit = llvm::Module::Pointer64 ==
    B.GetInsertBlock()->getParent()->getParent()->getPointerSize();
  LLVMType *intPtrTy = is64Bit ? Type::getInt64Ty(Context) :
    Type::getInt32Ty(Context);

  // Receiver as an integer
  Value *receiverSmallObject = B.CreatePtrToInt(receiver, intPtrTy);
  // Receiver is a small object...
  receiverSmallObject =
    B.CreateAnd(receiverSmallObject, is64Bit ? 7 : 1);
  // Receiver is not a small object.
  receiverSmallObject =
    B.CreateICmpNE(receiverSmallObject, Constant::getNullValue(intPtrTy));
  // Ideally, we'd call objc_msgSend() here, but for now just skip the cache
  // lookup

  Value *isCacheEmpty = 
    B.CreateICmpEQ(versionValue, Constant::getNullValue(IntTy));
  Value *receiverNil =
     B.CreateICmpEQ(receiver, Constant::getNullValue(receiver->getType()));

  isCacheEmpty = B.CreateOr(isCacheEmpty, receiverNil);
  isCacheEmpty = B.CreateOr(isCacheEmpty, receiverSmallObject);
      
  BasicBlock *cacheLookupBB = BasicBlock::Create(Context, "cache_check",
      lookupBB->getParent());

  B.CreateCondBr(isCacheEmpty, lookupBB, cacheLookupBB);

  // Check the cache node is current
  B.SetInsertPoint(cacheLookupBB);
  Value *slotValue = B.CreateLoad(slot, "slot_value");
  Value *slotVersion = B.CreateStructGEP(slotValue, 3);
  // Note: Volatile load because the slot version might have changed in
  // another thread.
  slotVersion = B.CreateLoad(slotVersion, true, "slot_version");
  Value *slotCachedFor = B.CreateStructGEP(slotValue, 1);
  slotCachedFor = B.CreateLoad(slotCachedFor, true, "slot_owner");
  Value *cls = B.CreateLoad(B.CreateBitCast(receiver, IdTy));
  Value *isVersionCorrect = B.CreateICmpEQ(slotVersion, versionValue);
  Value *isOwnerCorrect = B.CreateICmpEQ(slotCachedFor, cls);
  Value *isSlotValid = B.CreateAnd(isVersionCorrect, isOwnerCorrect);
  // If this slot is still valid, skip the lookup.
  B.CreateCondBr(isSlotValid, afterLookupBB, lookupBB);

  // Perform the real lookup and cache the result
  removeTerminator(lookupFinishedBB);
  // Replace the looked up slot with the loaded one
  B.SetInsertPoint(afterLookupBB, afterLookupBB->begin());
  PHINode *newLookup = IRBuilderCreatePHI(&B, lookup->getType(), 3, "new_lookup");
  // Not volatile, so a redundant load elimination pass can do some phi
  // magic with this later.
  lookup->replaceAllUsesWith(newLookup);

  B.SetInsertPoint(lookupFinishedBB);
  Value * newReceiver = receiver;
  if (!isSuperMessage) {
    newReceiver = B.CreateLoad(receiverPtr);
  }
  BasicBlock *storeCacheBB = BasicBlock::Create(Context, "cache_store",
      lookupBB->getParent());

  // Don't store the cached lookup if we are doing forwarding tricks.
  // Also skip caching small object messages for now
  Value *skipCacheWrite =
    B.CreateOr(B.CreateICmpNE(receiver, newReceiver), receiverSmallObject);
  skipCacheWrite = B.CreateOr(skipCacheWrite, receiverNil);
  B.CreateCondBr(skipCacheWrite, afterLookupBB, storeCacheBB);
  B.SetInsertPoint(storeCacheBB);

  // Store it even if the version is 0, because we always check that the
  // version is not 0 at the start and an occasional redundant store is
  // probably better than a branch every time.
  B.CreateStore(lookup, slot);
  B.CreateStore(B.CreateLoad(B.CreateStructGEP(lookup, 3)), version);
  cls = B.CreateLoad(B.CreateBitCast(receiver, IdTy));
  B.CreateStore(cls, B.CreateStructGEP(lookup, 1));
  B.CreateBr(afterLookupBB);

  newLookup->addIncoming(lookup, lookupFinishedBB);
  newLookup->addIncoming(slotValue, cacheLookupBB);
  newLookup->addIncoming(lookup, storeCacheBB);
}


void GNUstep::IMPCacher::SpeculativelyInline(Instruction *call, Function
    *function) {
  BasicBlock *beforeCallBB = call->getParent();
  BasicBlock *callBB = SplitBlock(beforeCallBB, call, Owner);
  BasicBlock *inlineBB = BasicBlock::Create(Context, "inline",
      callBB->getParent());


  BasicBlock::iterator iter = call;
  iter++;

  BasicBlock *afterCallBB = SplitBlock(iter->getParent(), iter, Owner);

  removeTerminator(beforeCallBB);

  // Put a branch before the call, testing whether the callee really is the
  // function
  IRBuilder<> B = IRBuilder<>(beforeCallBB);
  Value *callee = isa<CallInst>(call) ? cast<CallInst>(call)->getCalledValue()
      : cast<InvokeInst>(call)->getCalledValue();

  const FunctionType *FTy = function->getFunctionType();
  const FunctionType *calleeTy = cast<FunctionType>(
      cast<PointerType>(callee->getType())->getElementType());
  if (calleeTy != FTy) {
    callee = B.CreateBitCast(callee, function->getType());
  }

  Value *isInlineValid = B.CreateICmpEQ(callee, function);
  B.CreateCondBr(isInlineValid, inlineBB, callBB);

  // In the inline BB, add a copy of the call, but this time calling the real
  // version.
  Instruction *inlineCall = call->clone();
  Value *inlineResult= inlineCall;
  inlineBB->getInstList().push_back(inlineCall);

  B.SetInsertPoint(inlineBB);

  if (calleeTy != FTy) {
    for (unsigned i=0 ; i<FTy->getNumParams() ; i++) {
      LLVMType *callType = calleeTy->getParamType(i);
      LLVMType *argType = FTy->getParamType(i);
      if (callType != argType) {
        inlineCall->setOperand(i, new
            BitCastInst(inlineCall->getOperand(i), argType, "", inlineCall));
      }
    }
    if (FTy->getReturnType() != calleeTy->getReturnType()) {
      if (FTy->getReturnType() == Type::getVoidTy(Context)) {
        inlineResult = Constant::getNullValue(calleeTy->getReturnType());
      } else {
        inlineResult = 
          new BitCastInst(inlineCall, calleeTy->getReturnType(), "", inlineBB);
      }
    }
  }

  B.CreateBr(afterCallBB);

  // Unify the return values
  if (call->getType() != Type::getVoidTy(Context)) {
    PHINode *phi = CreatePHI(call->getType(), 2, "", afterCallBB->begin());
    call->replaceAllUsesWith(phi);
    phi->addIncoming(call, callBB);
    phi->addIncoming(inlineResult, inlineBB);
  }

  // Really do the real inlining
  InlineFunctionInfo IFI(0, 0);
  if (CallInst *c = dyn_cast<CallInst>(inlineCall)) {
    c->setCalledFunction(function);
    InlineFunction(c, IFI);
  } else if (InvokeInst *c = dyn_cast<InvokeInst>(inlineCall)) {
    c->setCalledFunction(function);
    InlineFunction(c, IFI);
  }
}

CallSite GNUstep::IMPCacher::SplitSend(CallSite msgSend)
{
  BasicBlock *lookupBB = msgSend->getParent();
  Function *F = lookupBB->getParent();
  Module *M = F->getParent();
  Function *send = M->getFunction("objc_msgSend");
  Function *send_stret = M->getFunction("objc_msgSend_stret");
  Function *send_fpret = M->getFunction("objc_msgSend_fpret");
  Value *self;
  Value *cmd;
  int selfIndex = 0;
  if ((msgSend.getCalledFunction() == send) ||
      (msgSend.getCalledFunction() == send_fpret)) {
    self = msgSend.getArgument(0);
    cmd = msgSend.getArgument(1);
  } else if (msgSend.getCalledFunction() == send_stret) {
    selfIndex = 1;
    self = msgSend.getArgument(1);
    cmd = msgSend.getArgument(2);
  } else {
    abort();
    return CallSite();
  }
  CGBuilder B(&F->getEntryBlock(), F->getEntryBlock().begin());
  Value *selfPtr = B.CreateAlloca(self->getType());
  B.SetInsertPoint(msgSend.getInstruction());
  B.CreateStore(self, selfPtr, true);
  LLVMType *impTy = msgSend.getCalledValue()->getType();
  LLVMType *slotTy = PointerType::getUnqual(StructType::get(PtrTy, PtrTy, PtrTy,
        IntTy, impTy, PtrTy, NULL));
  Value *slot;
  Constant *lookupFn = M->getOrInsertFunction("objc_msg_lookup_sender",
      slotTy, selfPtr->getType(), cmd->getType(), PtrTy, NULL);
  if (msgSend.isCall()) {
    slot = B.CreateCall3(lookupFn, selfPtr, cmd, Constant::getNullValue(PtrTy));
  } else {
    InvokeInst *inv = cast<InvokeInst>(msgSend.getInstruction());
    BasicBlock *callBB = SplitBlock(lookupBB, msgSend.getInstruction(), Owner);
    removeTerminator(lookupBB);
    B.SetInsertPoint(lookupBB);
    slot = B.CreateInvoke3(lookupFn, callBB, inv->getUnwindDest(), selfPtr, cmd,
        Constant::getNullValue(PtrTy));
    addPredecssor(inv->getUnwindDest(), msgSend->getParent(), lookupBB);
    B.SetInsertPoint(msgSend.getInstruction());
  }
  Value *imp = B.CreateLoad(B.CreateStructGEP(slot, 4));
  msgSend.setArgument(selfIndex, B.CreateLoad(selfPtr, true));
  msgSend.setCalledFunction(imp);
  return CallSite(slot);
}

// Cleanly removes a terminator instruction.
void GNUstep::removeTerminator(BasicBlock *BB) {
  TerminatorInst *BBTerm = BB->getTerminator();

  // Remove the BB as a predecessor from all of  successors
  for (unsigned i = 0, e = BBTerm->getNumSuccessors(); i != e; ++i) {
    BBTerm->getSuccessor(i)->removePredecessor(BB);
  }

  BBTerm->replaceAllUsesWith(UndefValue::get(BBTerm->getType()));
  // Remove the terminator instruction itself.
  BBTerm->eraseFromParent();
}
void GNUstep::addPredecssor(BasicBlock *block, BasicBlock *oldPredecessor,
    BasicBlock *newPredecessor) {
  for (BasicBlock::iterator i=block->begin() ; PHINode *phi=dyn_cast<PHINode>(i)
      ; ++i) {
    Value *v = phi->getIncomingValueForBlock(oldPredecessor);
    phi->addIncoming(v, newPredecessor);
  }
}
