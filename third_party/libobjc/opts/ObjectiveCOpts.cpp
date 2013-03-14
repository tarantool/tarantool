#include "LLVMCompat.h"
#if LLVM_MAJOR >= 3
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#include <llvm/PassManager.h>
#endif

#include "ObjectiveCOpts.h"

using namespace llvm;

namespace 
{
  class ObjectiveCOpts : public ModulePass {
    ModulePass *ClassIMPCachePass;
    ModulePass *ClassLookupCachePass;
    ModulePass *ClassMethodInliner;
    FunctionPass *GNUNonfragileIvarPass;
    FunctionPass *GNULoopIMPCachePass;

    public:
    static char ID;
    ObjectiveCOpts() : ModulePass(ID) {
      ClassIMPCachePass = createClassIMPCachePass();
      ClassLookupCachePass = createClassLookupCachePass();
      ClassMethodInliner = createClassMethodInliner();
      GNUNonfragileIvarPass = createGNUNonfragileIvarPass();
      GNULoopIMPCachePass = createGNULoopIMPCachePass();
    }
    virtual ~ObjectiveCOpts() {
      delete ClassIMPCachePass;
      delete ClassMethodInliner;
      delete ClassLookupCachePass;
      delete GNULoopIMPCachePass;
      delete GNUNonfragileIvarPass;
    }

    virtual bool runOnModule(Module &Mod) {
      bool modified;
      modified = ClassIMPCachePass->runOnModule(Mod);
      modified |= ClassLookupCachePass->runOnModule(Mod);
      modified |= ClassMethodInliner->runOnModule(Mod);

      for (Module::iterator F=Mod.begin(), fend=Mod.end() ;
          F != fend ; ++F) {

        if (F->isDeclaration()) { continue; }
        modified |= GNUNonfragileIvarPass->runOnFunction(*F);
        modified |= GNULoopIMPCachePass->runOnFunction(*F);
      }

      return modified;
    };
  };

  char ObjectiveCOpts::ID = 0;
  RegisterPass<ObjectiveCOpts> X("gnu-objc", 
          "Run all of the GNUstep Objective-C runtimm optimisations");


#if LLVM_MAJOR >= 3

  void addObjCPasses(const PassManagerBuilder &Builder, PassManagerBase &PM) {
    // Always add the ivar simplification pass
    PM.add(createGNUNonfragileIvarPass());
    // Only cache IMPs in loops if we're not optimising for size.
    if (Builder.SizeLevel == 0) {
      PM.add(createGNULoopIMPCachePass());
    }
    // Do the rest of the caching if we're not aggressively optimising for size
    if (Builder.SizeLevel < 2) {
      PM.add(createClassIMPCachePass());
      PM.add(createClassLookupCachePass());
    }
    // Definitely don't do extra inlining if we're optimising for size!
    if (Builder.SizeLevel == 0) {
      PM.add(createClassMethodInliner());
    }
  }
  /*
  static struct PluginRegister {
    PluginRegister() {
      PassManagerBuilder::addGlobalExtension(PassManagerBuilder::EP_LoopOptimizerEnd,
                                                   addObjCPasses);
    }
  } Register;
  */
  RegisterStandardPasses S(PassManagerBuilder::EP_LoopOptimizerEnd,
                           addObjCPasses);
#endif

}
