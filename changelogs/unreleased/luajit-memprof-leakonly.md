## feature/luajit

* Now memory profiler parser reports heap difference occurring during
  the measurement interval (gh-5812). New memory profiler's option
  `--leak-only` shows only heap difference is introduced. New built-in
  module `memprof.process` is introduced to perform memory events
  post-processing and aggregation. Now to launch memory profiler
  via Tarantool user should use the following command:
  `tarantool -e 'require("memprof")(arg)' - --leak-only /tmp/memprof.bin`
