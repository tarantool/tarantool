## bugfix/swim

* Fix `<swim_instance>:broadcast()` which does not work on non-local addresses
  and spams "Permission denied" errors to the log. Also after instance
  termination it could return a non-0 exit code even if there was no errors in
  the script, and spam the error again (gh-5864).
