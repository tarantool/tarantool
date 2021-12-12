## feature/luajit

* Now memory profiler records allocations from traces grouping them by the
  trace number (gh-5814). The memory profiler parser can display the new type
  of allocation sources in the following format:
  ```
  | TRACE [<trace-no>] <trace-addr> started at @<sym-chunk>:<sym-line>
  ```
