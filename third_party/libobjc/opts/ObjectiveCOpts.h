llvm::ModulePass *createClassIMPCachePass(void);
llvm::ModulePass *createClassLookupCachePass(void);
llvm::ModulePass *createClassMethodInliner(void);
llvm::FunctionPass *createGNUNonfragileIvarPass(void);
llvm::FunctionPass *createGNULoopIMPCachePass(void);
llvm::ModulePass *createTypeFeedbackPass(void);
llvm::ModulePass *createTypeFeedbackDrivenInlinerPass(void);
