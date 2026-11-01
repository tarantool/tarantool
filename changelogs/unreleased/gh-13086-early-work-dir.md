## bugfix/config

* Configuration now creates and enters `process.work_dir` after initial
  configuration validation and before startup appliers, including early-loaded
  roles and applications. Their ordinary relative filesystem operations now use
  the instance workdir instead of the startup directory. Initial validation may
  still execute module code while evaluating defaults before directory entry.
  There is no compatibility opt-out. Relative workdirs keep their startup-cwd
  interpretation on reload; direct script-driven `box.cfg()` is unchanged
  (gh-13086).
