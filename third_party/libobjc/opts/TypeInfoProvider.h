#include "LLVMCompat.h"
#include "IMPCacher.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/ADT/StringMap.h"

using namespace llvm;
namespace GNUstep
{
  class TypeInfoProvider
  {
    public:
      typedef StringMap<uintptr_t> CallSiteEntry;
      //typedef std::vector<CallSiteEntry> CallSiteMap;
      typedef SmallVector<CallSiteEntry, 16> CallSiteMap;
    private:
      struct callsite_info
      {
        uintptr_t moduleID;
        int32_t callsiteID;
        uintptr_t methodID;
      };
      const char *symbol_table;
      size_t symbol_size;
      StringMap<CallSiteMap> CallSiteRecords;

      void loadCallsiteRecords(callsite_info *callsite_records, size_t size);
      TypeInfoProvider(void);

    public:
      CallSiteMap* getCallSitesForModule(Module &M);
      void PrintStatistics();
      static TypeInfoProvider* SharedTypeInfoProvider();
      ~TypeInfoProvider();
  };

}
