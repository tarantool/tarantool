#include "LLVMCompat.h"
#include "llvm/Analysis/Verifier.h"
#include "llvm/ADT/DenseSet.h"
#include "ObjectiveCOpts.h"
#include <string>

using namespace llvm;
using std::string;

typedef std::pair<Instruction*, Value*> Replacement;

namespace llvm {
template<> struct DenseMapInfo<Replacement> {
  static inline Replacement getEmptyKey() { return Replacement(0,0); }
  static inline Replacement getTombstoneKey() { return Replacement(0, (Value*)-1); }
  static unsigned getHashValue(const Replacement& Val) { return ((uintptr_t)Val.first) * 37U; }
  static bool isEqual(const Replacement& LHS, const Replacement& RHS) {
    return LHS.first == RHS.first;
  }
};
}
namespace {
  class GNUNonfragileIvarPass : public FunctionPass {

    public:
    static char ID;
    GNUNonfragileIvarPass() : FunctionPass(ID) {}

    Module *M;
    size_t PointerSize;
    virtual bool doInitialization(Module &Mod) {
      M = &Mod;
      PointerSize = 8;
      if (M->getPointerSize() == Module::Pointer32) 
        PointerSize = 4;
      return false;  
    }

    std::string getSuperName(Constant *ClsStruct) {
      User *super = cast<User>(ClsStruct->getOperand(1));
      if (isa<ConstantPointerNull>(super)) return "";
      GlobalVariable *name = cast<GlobalVariable>(super->getOperand(0));
#if (LLVM_MAJOR > 3) || ((LLVM_MAJOR == 3) && (LLVM_MINOR > 0))
      return cast<ConstantDataArray>(name->getInitializer())->getAsString();
#else
      return cast<ConstantArray>(name->getInitializer())->getAsString();
#endif
    }

    size_t sizeOfClass(const std::string &className) {
      // This is a root class
      if ("" == className) { return 0; }
      // These root classes are assumed to only have one ivar: isa
      if (className.compare(0, 8, "NSObject") == 0 || 
          className.compare(0, 6, "Object") == 0) {
        return PointerSize;
      }
      GlobalVariable *Cls = M->getGlobalVariable("_OBJC_CLASS_" + className);
      if (!Cls) return 0;
      Constant *ClsStruct = Cls->getInitializer();
      // Size is initialized to be negative for the non-fragile ABI.
      ConstantInt *Size = cast<ConstantInt>(ClsStruct->getOperand(5));
      int s = Size->getSExtValue();
      // If we find a fragile class in the hierarchy, don't perform the
      // simplification.  This means that we're the mixed ABI, so we need the
      // extra indirection.
      if (s > 0) return 0;
      return sizeOfClass(getSuperName(ClsStruct)) - Size->getSExtValue();
    }

    size_t hardCodedOffset(const StringRef &className, 
                           const StringRef &ivarName) {
      GlobalVariable *Cls = M->getGlobalVariable(("_OBJC_CLASS_" + className).str(), true);
      if (!Cls) return 0;
      Constant *ClsStruct = Cls->getInitializer();
      size_t superSize = sizeOfClass(getSuperName(ClsStruct));
      if (!superSize) return 0;
      ConstantStruct *IvarStruct = cast<ConstantStruct>(
          cast<GlobalVariable>(ClsStruct->getOperand(6))->getInitializer());
      int ivarCount = cast<ConstantInt>(IvarStruct->getOperand(0))->getSExtValue();
      Constant *ivars = IvarStruct->getOperand(1);
      for (int i=0 ; i<ivarCount ; i++) {
        Constant *ivar = cast<Constant>(ivars->getOperand(i));
        GlobalVariable *name =
          cast<GlobalVariable>(
              cast<User>(ivar->getOperand(0))->getOperand(0));
        std::string ivarNameStr = 
#if (LLVM_MAJOR > 3) || ((LLVM_MAJOR == 3) && (LLVM_MINOR > 0))
          cast<ConstantDataArray>(name->getInitializer())->getAsString();
#else
          cast<ConstantArray>(name->getInitializer())->getAsString();
#endif
        // Remove the NULL terminator from the metadata string
        ivarNameStr.resize(ivarNameStr.size() - 1);
        if (ivarNameStr == ivarName.str())
          return superSize +
            cast<ConstantInt>(ivar->getOperand(2))->getSExtValue();
      }
      return 0;
    }

    virtual bool runOnFunction(Function &F) {
      bool modified = false;
      llvm::DenseSet<Replacement> replacements;
      //llvm::cerr << "IvarPass: " << F.getName() << "\n";
      for (Function::iterator i=F.begin(), end=F.end() ;
          i != end ; ++i) {
        for (BasicBlock::iterator b=i->begin(), last=i->end() ;
            b != last ; ++b) {
          if (LoadInst *indirectload = dyn_cast<LoadInst>(b)) {
            if (LoadInst *load = dyn_cast<LoadInst>(indirectload->getOperand(0))) {
              if (GlobalVariable *ivar =
                  dyn_cast<GlobalVariable>(load->getOperand(0))) {
                StringRef variableName = ivar->getName();

                if (!variableName.startswith("__objc_ivar_offset_")) break;

                static size_t prefixLength = strlen("__objc_ivar_offset_");

                StringRef suffix = variableName.substr(prefixLength,
                    variableName.size()-prefixLength);

                std::pair<StringRef,StringRef> parts = suffix.split('.');
                StringRef className = parts.first;
                StringRef ivarName = parts.second;

                // If the class, and all superclasses, are visible in this module
                // then we can hard-code the ivar offset
                if (size_t offset = hardCodedOffset(className, ivarName)) {
                  replacements.insert(Replacement(indirectload,
                              ConstantInt::get(indirectload->getType(), offset)));
                  replacements.insert(Replacement(load, 0));
                  modified = true;
                } else {
                  // If the class was compiled with the new ABI, then we have a
                  // direct offset variable that we can use
                  if (Value *offset = M->getGlobalVariable(
                              ("__objc_ivar_offset_value_" + suffix).str())) {
                    replacements.insert(Replacement(load, offset));
                    modified = true;
                  }
                }
              }
            }
          }
        }
      }
      for (DenseSet<Replacement>::iterator i=replacements.begin(),
              end=replacements.end() ; i != end ; ++i) {
        if (i->second) 
          i->first->replaceAllUsesWith(i->second);
      }
      verifyFunction(F);
      return modified;
    }
  };

  char GNUNonfragileIvarPass::ID = 0;
  RegisterPass<GNUNonfragileIvarPass> X("gnu-nonfragile-ivar", "Ivar fragility pass");
}

FunctionPass *createGNUNonfragileIvarPass(void)
{
  return new GNUNonfragileIvarPass();
}
