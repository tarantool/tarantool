## bugfix/config

* Fixed missing module-dependent defaults on startup when `process.work_dir`
  uses a context value from the configuration, an environment variable,
  or a file with an absolute path (gh-13086).
* Configuration now reuses values read from the same environment variable
  or file within one configuration build, so changes during module loading
  do not produce inconsistent configuration values (gh-13086).
