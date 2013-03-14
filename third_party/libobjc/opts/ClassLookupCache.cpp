#include "llvm/ADT/StringMap.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "ObjectiveCOpts.h"
#include <string>

#include "IMPCacher.h"
#include "LLVMCompat.h"

using namespace llvm;
using namespace GNUstep;
using std::string;
using std::pair;

namespace 
{
  class ClassLookupCachePass : public ModulePass {
    /// Module that we're currently optimising
    Module *M;
    /// Static cache.  If we're not using the non-fragile ABI, then we cache
    /// all class lookups in static variables to avoid the overhead of the
    /// lookup.  With the non-fragile ABI, we don't need to do this.
    llvm::StringMap<GlobalVariable*> statics;

    typedef std::pair<CallInst*,std::string> ClassLookup;

    public:
    static char ID;
    ClassLookupCachePass() : ModulePass(ID) {}

    virtual bool doInitialization(Module &Mod) {
      M = &Mod;
      return false;  
    }

    bool runOnFunction(Function &F) {
      bool modified = false;
      SmallVector<ClassLookup, 16> Lookups;

      for (Function::iterator i=F.begin(), end=F.end() ;
          i != end ; ++i) {
        for (BasicBlock::iterator b=i->begin(), last=i->end() ;
            b != last ; ++b) {
          if (CallInst *call = dyn_cast<CallInst>(b)) {
            if (Function *func = dyn_cast<Function>(call->getCalledValue()->stripPointerCasts())) {
              if (func->getName() == "objc_lookup_class") {
                ClassLookup lookup;
                GlobalVariable *classNameVar = dyn_cast<GlobalVariable>(
                    call->getOperand(0)->stripPointerCasts());
                if (0 == classNameVar) { continue; }
#if (LLVM_MAJOR > 3) || ((LLVM_MAJOR == 3) && (LLVM_MINOR > 0))
                ConstantDataArray *init = dyn_cast<ConstantDataArray>(
                    classNameVar->getInitializer());
#else
                ConstantArray *init = dyn_cast<ConstantArray>(
                    classNameVar->getInitializer());
#endif
                if (0 == init || !init->isCString()) { continue; }
                lookup.first = call;
                lookup.second = init->getAsString();
                modified = true;
                Lookups.push_back(lookup);
              }
            }
          }
        }
      }
      for (SmallVectorImpl<ClassLookup>::iterator i=Lookups.begin(), 
          e=Lookups.end() ; e!=i ; i++) {
        llvm::Instruction *lookup = i->first;
        std::string &cls = i->second;
        LLVMType *clsTy = lookup->getType();
        Value *global = M->getGlobalVariable(("_OBJC_CLASS_" + i->second).c_str(), true);
        // If we can see the class reference for this, then reference it
        // directly.  If not, then do the lookup and cache it.
        if (global) {
          // Insert a bitcast of the class to the required type where the
          // lookup is and then replace all references to the lookup with it.
          Value *cls = new BitCastInst(global, clsTy, "class", lookup);
          lookup->replaceAllUsesWith(cls);
          lookup->removeFromParent();
          delete lookup;
        } else {
          GlobalVariable *cache = statics[cls];
          if (!cache) {
            cache = new GlobalVariable(*M, clsTy, false,
                GlobalVariable::PrivateLinkage, Constant::getNullValue(clsTy),
                ".class_cache");
            statics[cls] = cache;
          }
          BasicBlock *beforeLookupBB = lookup->getParent();
          BasicBlock *lookupBB = SplitBlock(beforeLookupBB, lookup, this);
          BasicBlock::iterator iter = lookup;
          iter++;
          BasicBlock *afterLookupBB = SplitBlock(iter->getParent(), iter, this);
          // SplitBlock() adds an unconditional branch, which we don't want.
          // Remove it.
          removeTerminator(beforeLookupBB);
          removeTerminator(lookupBB);

          PHINode *phi = CreatePHI(clsTy, 2, cls, afterLookupBB->begin());
          // We replace all of the existing uses with the PHI node now, because
          // we're going to add some more uses later that we don't want
          // replaced.
          lookup->replaceAllUsesWith(phi);

          // In the original basic block, we test whether the cache is NULL,
          // and skip the lookup if it isn't.
          IRBuilder<> B(beforeLookupBB);
          llvm::Value *cachedClass =
            B.CreateBitCast(B.CreateLoad(cache), clsTy);
          llvm::Value *needsLookup = B.CreateIsNull(cachedClass);
          B.CreateCondBr(needsLookup, lookupBB, afterLookupBB);
          // In the lookup basic block, we just do the lookup, store it in the
          // cache, and then jump to the continue block
          B.SetInsertPoint(lookupBB);
          B.CreateStore(lookup, cache);
          B.CreateBr(afterLookupBB);
          // Now we just need to set the PHI node to use the cache or the
          // lookup result
          phi->addIncoming(cachedClass, beforeLookupBB);
          phi->addIncoming(lookup, lookupBB);
        }
      }
      return modified;
    }
    virtual bool runOnModule(Module &Mod) {
      statics.empty();
      M = &Mod;
      bool modified = false;

      for (Module::iterator F=Mod.begin(), fend=Mod.end() ;
          F != fend ; ++F) {

        if (F->isDeclaration()) { continue; }

        modified |= runOnFunction(*F);
      }

      return modified;
    };
  };

  char ClassLookupCachePass::ID = 0;
  RegisterPass<ClassLookupCachePass> X("gnu-class-lookup-cache", 
          "Cache class lookups");
}

ModulePass *createClassLookupCachePass(void)
{
  return new ClassLookupCachePass();
}
