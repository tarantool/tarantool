## feature/lua/popen

* Introduced new option `inherit_fds` for `popen.new`. The option takes
  an array of file descriptor numbers that should be left open in the child
  process (gh-8926).
