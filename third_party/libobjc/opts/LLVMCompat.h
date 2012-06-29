/**
 * Compatibility header that wraps LLVM API breakage and lets us compile with
 * old and new versions of LLVM.
 *
 * First LLVM version supported is 2.9.
 */

#ifndef __LANGUAGEKIT_LLVM_HACKS__
#define __LANGUAGEKIT_LLVM_HACKS__
#include <llvm/Instructions.h>
#include <llvm/Metadata.h>
#include <llvm/Intrinsics.h>
#include <llvm/Support/IRBuilder.h>


// Only preserve names in a debug build.  This simplifies the
// IR in a release build, but makes it much harder to debug.
#ifndef DEBUG
	typedef llvm::IRBuilder<false> CGBuilder;
#else
	typedef llvm::IRBuilder<> CGBuilder;
#endif

__attribute((unused)) static inline 
llvm::PHINode* CreatePHI(llvm::Type *Ty,
                         unsigned NumReservedValues,
                         const llvm::Twine &NameStr="",
                         llvm::Instruction *InsertBefore=0) {
#if LLVM_MAJOR < 3
    llvm::PHINode *phi = llvm::PHINode::Create(Ty, NameStr, InsertBefore);
    phi->reserveOperandSpace(NumReservedValues);
    return phi;
#else
    return llvm::PHINode::Create(Ty, NumReservedValues, NameStr, InsertBefore);
#endif
}

__attribute((unused)) static inline 
llvm::PHINode* IRBuilderCreatePHI(CGBuilder *Builder,
                                  llvm::Type *Ty,
                                  unsigned NumReservedValues,
                                  const llvm::Twine &NameStr="")
{
#if LLVM_MAJOR < 3
	llvm::PHINode *phi = Builder->CreatePHI(Ty, NameStr);
	phi->reserveOperandSpace(NumReservedValues);
	return phi;
#else
	return Builder->CreatePHI(Ty, NumReservedValues, NameStr);
#endif
}



__attribute((unused)) static inline 
llvm::MDNode* CreateMDNode(llvm::LLVMContext &C,
                           llvm::Value **V,
                            unsigned length=1) {
#if LLVM_MAJOR < 3
	return llvm::MDNode::get(C, V, length);
#else
	llvm::ArrayRef<llvm::Value*> val(V, length);
	return llvm::MDNode::get(C, val);
#endif
}

template<typename T>
static inline 
llvm::InvokeInst* IRBuilderCreateInvoke(CGBuilder *Builder,
                                        llvm::Value *callee,
                                        llvm::BasicBlock *dest,
                                        llvm::BasicBlock *dest2,
                                        T values,
                                        const llvm::Twine &NameStr="")
{
#if LLVM_MAJOR < 3
	return Builder->CreateInvoke(callee, dest, dest2, values.begin(), values.end(), NameStr);
#else
	return Builder->CreateInvoke(callee, dest, dest2, values, NameStr);
#endif
}

template<typename T>
static inline 
llvm::CallInst* IRBuilderCreateCall(CGBuilder *Builder,
                                    llvm::Value *callee,
                                    T values,
                                    const llvm::Twine &NameStr="")
{
#if LLVM_MAJOR < 3
	return Builder->CreateCall(callee, values.begin(), values.end(), NameStr);
#else
	return Builder->CreateCall(callee, values, NameStr);
#endif
}

template<typename T>
static inline 
llvm::CallInst* CreateCall(llvm::Value *callee,
                           T values,
                           const llvm::Twine &NameStr,
                           llvm::Instruction *before)
{
#if LLVM_MAJOR < 3
	return llvm::CallInst::Create(callee, values.begin(), values.end(), NameStr, before);
#else
	return llvm::CallInst::Create(callee, values, NameStr, before);
#endif
}



#if LLVM_MAJOR < 3
#define GetStructType(context, ...) StructType::get(context, __VA_ARGS__)
#else
#define GetStructType(context, ...) StructType::get(__VA_ARGS__)
#endif

__attribute((unused)) static inline 
llvm::Constant* GetConstantStruct(llvm::LLVMContext &C, const std::vector<llvm::Constant*>
		&V, bool Packed) {
#if LLVM_MAJOR < 3
	return llvm::ConstantStruct::get(C, V, Packed);
#else
	return llvm::ConstantStruct::getAnon(C, V, Packed);
#endif
}


#if LLVM_MAJOR < 3
typedef const llvm::Type LLVMType;
typedef const llvm::StructType LLVMStructType;
typedef const llvm::ArrayType LLVMArrayType;
typedef const llvm::PointerType LLVMPointerType;
typedef const llvm::IntegerType LLVMIntegerType;
#else
typedef llvm::Type LLVMType;
typedef llvm::StructType LLVMStructType;
typedef llvm::ArrayType LLVMArrayType;
typedef llvm::PointerType LLVMPointerType;
typedef llvm::IntegerType LLVMIntegerType;
#endif

#endif
