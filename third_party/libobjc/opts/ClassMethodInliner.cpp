#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/Instructions.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Constants.h"
#include "llvm/LLVMContext.h"
#include "llvm/GlobalVariable.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/DefaultPasses.h"
#include "ObjectiveCOpts.h"
#include "IMPCacher.h"
#include <string>

using namespace llvm;
using namespace GNUstep;
using std::string;

// Mangle a method name
//
// From clang:
static std::string SymbolNameForMethod(const std::string &ClassName, const
      std::string &CategoryName, const std::string &MethodName, bool isClassMethod)
{
    std::string MethodNameColonStripped = MethodName;
      std::replace(MethodNameColonStripped.begin(), MethodNameColonStripped.end(),
                ':', '_');
        return std::string(isClassMethod ? "_c_" : "_i_") + ClassName + "_" +
              CategoryName + "_" + MethodNameColonStripped;
}

namespace 
{
  class ClassMethodInliner : public ModulePass 
  {
    const IntegerType *IntTy;

    public:
    static char ID;
    ClassMethodInliner() : ModulePass(ID) {}

    virtual bool runOnModule(Module &M) {
      unsigned MessageSendMDKind = M.getContext().getMDKindID("GNUObjCMessageSend");
      InlineCostAnalyzer CA;
      SmallPtrSet<const Function *, 16> NeverInline;

      GNUstep::IMPCacher cacher = GNUstep::IMPCacher(M.getContext(), this);
      IntTy = (sizeof(int) == 4 ) ? Type::getInt32Ty(M.getContext()) :
          Type::getInt64Ty(M.getContext()) ;
      bool modified = false;

      for (Module::iterator F=M.begin(), fend=M.end() ;
          F != fend ; ++F) {

        SmallVector<CallSite, 16> messages;

        if (F->isDeclaration()) { continue; }

        for (Function::iterator i=F->begin(), end=F->end() ;
            i != end ; ++i) {
          for (BasicBlock::iterator b=i->begin(), last=i->end() ;
              b != last ; ++b) {
            CallSite call(b);
            if (call.getInstruction() && !call.getCalledFunction()) {
              MDNode *messageType = call->getMetadata(MessageSendMDKind);
              if (0 == messageType) { continue; }
              messages.push_back(call);
            }
          }
        }
        for (SmallVectorImpl<CallSite>::iterator i=messages.begin(), 
            e=messages.end() ; e!=i ; i++) {

          MDNode *messageType = (*i)->getMetadata(MessageSendMDKind);
          StringRef sel = 
                cast<MDString>(messageType->getOperand(0))->getString();
          StringRef cls = 
                cast<MDString>(messageType->getOperand(1))->getString();
          bool isClassMethod = 
                cast<ConstantInt>(messageType->getOperand(2))->isOne();
          std::string functionName = SymbolNameForMethod(cls, "", sel, isClassMethod);
          Function *method = M.getFunction(functionName);

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
            i->getInstruction()->setMetadata(MessageSendMDKind, 0);
            modified = true;
          }
        }
      }
      return modified;
    }
  };

  char ClassMethodInliner::ID = 0;
  RegisterPass<ClassMethodInliner> X("gnu-class-method-inline", 
          "Inline class methods and message sends to super");
}

ModulePass *createClassMethodInliner(void)
{
  return new ClassMethodInliner();
}
