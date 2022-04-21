## feature/luajit
* Now memory profiler dumps symbol table for C-functions. As a result memory profiler parser can enrich its symbol table with C-symbols (gh-5813). Furthermore, non memprof dumps special symbol table events when it encounters a new C-symbol, that haven't been dumped yet.
