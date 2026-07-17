## bugfix/config

* Fixed a spurious `The vshard-ee/vshard module is not available`
  configuration validation error on startup when the vshard module is
  installed into the configured `process.work_dir` and the process is started
  from a different directory, or when an instance without a sharding role
  inherits a global sharding option, such as `sharding.bucket_count`
  (gh-12928). The availability of vshard and its minimum version are checked
  when the sharding configuration is applied, after `box.cfg()`, when the
  configured working directory is already entered.
