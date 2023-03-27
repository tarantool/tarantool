## feature/lua/popen

* Eliminated polling in `<popen handle>:wait()`, so now it reacts to SIGCHLD
  faster and performs less unnecessary work (gh-4915).
