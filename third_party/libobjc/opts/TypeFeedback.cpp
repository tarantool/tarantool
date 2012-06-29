#include "llvm/Constants.h"
#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/IRBuilder.h"
#include "llvm/Linker.h"
#include <vector>

using namespace llvm;
#include "LLVMCompat.h"

namespace {
  struct GNUObjCTypeFeedback : public ModulePass {
      
    typedef std::pair<CallInst*,CallInst*> callPair;
    typedef std::vector<callPair > replacementVector;
      static char ID;
    uint32_t callsiteCount;
    LLVMIntegerType *Int32Ty;
      GNUObjCTypeFeedback() : ModulePass(ID), callsiteCount(0) {}

    void profileFunction(Function &F, Constant *ModuleID) {
      for (Function::iterator i=F.begin(), e=F.end() ;
                      i != e ; ++i) {

        Module *M = F.getParent();
        replacementVector replacements;
        for (BasicBlock::iterator b=i->begin(), last=i->end() ;
            b != last ; ++b) {

          CallSite call(b);
          if (call.getInstruction() && !call.getCalledFunction()) {
            llvm::SmallVector<llvm::Value*, 4> args;
            args.push_back(call->getOperand(1));
            args.push_back(call->getOperand(0)),
            args.push_back(ModuleID);
            args.push_back(ConstantInt::get(Int32Ty, callsiteCount++));
            Constant *profile = 
                M->getOrInsertFunction("objc_msg_profile",
                  Type::getVoidTy(M->getContext()),
                  args[0]->getType(), args[1]->getType(),
                  args[2]->getType(), args[3]->getType(), NULL);
            CreateCall(profile, args, "", call.getInstruction());
          }
        }
      }
    }

    public:
    virtual bool runOnModule(Module &M)
    {
      LLVMContext &VMContext = M.getContext();
      Int32Ty = IntegerType::get(VMContext, 32);
      LLVMPointerType *PtrTy = Type::getInt8PtrTy(VMContext);
      Constant *moduleName = 
#if (LLVM_MAJOR > 3) || ((LLVM_MAJOR == 3) && (LLVM_MINOR > 0))
        ConstantDataArray::getString(VMContext, M.getModuleIdentifier(), true);
#else
        ConstantArray::get(VMContext, M.getModuleIdentifier(), true);
#endif
      moduleName = new GlobalVariable(M, moduleName->getType(), true,
          GlobalValue::InternalLinkage, moduleName,
          ".objc_profile_module_name");
      std::vector<Constant*> functions;

      llvm::Constant *Zeros[2];
      Zeros[0] = ConstantInt::get(Type::getInt32Ty(VMContext), 0);
      Zeros[1] = Zeros[0];

      moduleName = ConstantExpr::getGetElementPtr(moduleName, Zeros, 2);
      functions.push_back(moduleName);;
      functions.push_back(moduleName);;

      for (Module::iterator F=M.begin(), e=M.end() ;
        F != e ; ++F) {
        if (F->isDeclaration()) { continue; }
        functions.push_back(ConstantExpr::getBitCast(F, PtrTy));

        Constant * ConstStr = 
#if (LLVM_MAJOR > 3) || ((LLVM_MAJOR == 3) && (LLVM_MINOR > 0))
          llvm::ConstantDataArray::getString(VMContext, F->getName(), true);
#else
          llvm::ConstantArray::get(VMContext, F->getName());
#endif
        ConstStr = new GlobalVariable(M, ConstStr->getType(), true,
            GlobalValue::PrivateLinkage, ConstStr, "str");
        functions.push_back(
            ConstantExpr::getGetElementPtr(ConstStr, Zeros, 2));

        profileFunction(*F, moduleName);
      }
      functions.push_back(ConstantPointerNull::get(PtrTy));
      Constant *symtab = ConstantArray::get(ArrayType::get(PtrTy,
            functions.size()), functions);
      Value *symbolTable = new GlobalVariable(M, symtab->getType(), true,
          GlobalValue::InternalLinkage, symtab, "symtab");

      Function *init =
        Function::Create(FunctionType::get(Type::getVoidTy(VMContext), false),
            GlobalValue::PrivateLinkage, "load_symbol_table", &M);
      BasicBlock * EntryBB = BasicBlock::Create(VMContext, "entry", init);
      IRBuilder<> B = IRBuilder<>(EntryBB);
      Value *syms = B.CreateStructGEP(symbolTable, 0);
      B.CreateCall(M.getOrInsertFunction("objc_profile_write_symbols",
            Type::getVoidTy(VMContext), syms->getType(), NULL),
          syms);
      B.CreateRetVoid();

      GlobalVariable *GCL = M.getGlobalVariable("llvm.global_ctors");

      std::vector<Constant*> ctors;

      ConstantArray *CA = cast<ConstantArray>(GCL->getInitializer());

      for (User::op_iterator i = CA->op_begin(), e = CA->op_end(); i != e; ++i) {
        ctors.push_back(cast<ConstantStruct>(*i));
      }

      // Type of one ctor
      LLVMType *ctorTy =
        cast<ArrayType>(GCL->getType()->getElementType())->getElementType();
      // Add the 
      std::vector<Constant*> CSVals;
      CSVals.push_back(ConstantInt::get(Type::getInt32Ty(VMContext),65535));
      CSVals.push_back(init);
      ctors.push_back(GetConstantStruct(GCL->getContext(), CSVals, false));
      // Create the array initializer.
      CA = cast<ConstantArray>(ConstantArray::get(ArrayType::get(ctorTy,
            ctors.size()), ctors));
      // Create the new global and replace the old one
      GlobalVariable *NGV = new GlobalVariable(CA->getType(),
          GCL->isConstant(), GCL->getLinkage(), CA, "", GCL->isThreadLocal());
      GCL->getParent()->getGlobalList().insert(GCL, NGV);
      NGV->takeName(GCL);
      GCL->replaceAllUsesWith(NGV);
      GCL->eraseFromParent();

      return true;
    }

  };
  
  char GNUObjCTypeFeedback::ID = 0;
  RegisterPass<GNUObjCTypeFeedback> X("gnu-objc-type-feedback", 
      "Objective-C type feedback for the GNU runtime.", false, true);
}

ModulePass *createTypeFeedbackPass(void) {
  return new GNUObjCTypeFeedback();
}
