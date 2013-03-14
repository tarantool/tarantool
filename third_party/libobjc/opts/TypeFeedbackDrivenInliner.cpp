#include "LLVMCompat.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Linker.h"
#include <vector>
#include "TypeInfoProvider.h"

using namespace llvm;
using namespace GNUstep;

namespace {
  struct GNUObjCTypeFeedbackDrivenInliner : public ModulePass {
      
    typedef std::pair<CallInst*,CallInst*> callPair;
    typedef std::vector<callPair > replacementVector;
      static char ID;
    uint32_t callsiteCount;
    const IntegerType *Int32Ty;


    public:

    GNUObjCTypeFeedbackDrivenInliner() : ModulePass(ID), callsiteCount(0) {}

    virtual bool runOnModule(Module &M)
    {
      bool modified = false;
      LLVMContext &VMContext = M.getContext();
      Int32Ty = IntegerType::get(VMContext, 32);
      TypeInfoProvider::CallSiteMap *SiteMap =
        TypeInfoProvider::SharedTypeInfoProvider()->getCallSitesForModule(M);
      SmallPtrSet<const Function *, 16> NeverInline;

      //TypeInfoProvider::SharedTypeInfoProvider()->PrintStatistics();
      GNUstep::IMPCacher cacher = GNUstep::IMPCacher(M.getContext(), this);
      InlineCostAnalyzer CA;
      SmallVector<CallSite, 16> messages;

      for (Module::iterator F=M.begin(), fend=M.end() ;
          F != fend ; ++F) {


        if (F->isDeclaration()) { continue; }

        for (Function::iterator i=F->begin(), end=F->end() ;
            i != end ; ++i) {
          for (BasicBlock::iterator b=i->begin(), last=i->end() ;
              b != last ; ++b) {
            CallSite call(b);
            if (call.getInstruction() && !call.getCalledFunction()) {
              messages.push_back(call);
            }
          }
        }
      }
      TypeInfoProvider::CallSiteMap::iterator Entry = SiteMap->begin();

      for (SmallVectorImpl<CallSite>::iterator i=messages.begin(), 
          e=messages.end() ; e!=i ; ++i, ++Entry) {

        if (Entry->size() == 1) {

          Function *method = M.getFunction(Entry->begin()->getKey());
          if (0 == method || method->isDeclaration()) { continue; }

#if (LLVM_MAJOR > 3) || ((LLVM_MAJOR == 3) && (LLVM_MINOR > 0))
          InlineCost IC = CA.getInlineCost((*i), method, 200);
#else
          InlineCost IC = CA.getInlineCost((*i), method, NeverInline);
#define getCost getValue
#endif
          // FIXME: 200 is a random number.  Pick a better one!
          if (IC.isAlways() || (IC.isVariable() && IC.getCost() < 200)) {
            cacher.SpeculativelyInline((*i).getInstruction(), method);
            modified = true;
          }
        }
        // FIXME: Inline the most popular call if one is much more popular
        // than the others.
      }
      return modified;
    }

  };
  
  char GNUObjCTypeFeedbackDrivenInliner::ID = 0;
  RegisterPass<GNUObjCTypeFeedbackDrivenInliner> X("gnu-objc-feedback-driven-inline", 
      "Objective-C type feedback-driven inliner for the GNU runtime.", false,
      true);
}

ModulePass *createTypeFeedbackDrivenInlinerPass(void) {
  return new GNUObjCTypeFeedbackDrivenInliner();
}
