list(APPEND tests
  lj-1152-stack-buffer-overflow-on-error.test.lua
  lj-1166-error-stitch-oom-ir-buff.test.lua
  lj-1166-error-stitch-oom-snap-buff.test.lua
  lj-1247-fin-tab-rehashing-on-trace.test.lua
  lj-1249-loadfile-fd-leak.test.lua
  lj-1298-oom-on-concat-recording.test.lua
)
BuildTestCLib(allocinject allocinject.c "${tests}")
