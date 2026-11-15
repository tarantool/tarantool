## feature/config

* Tarantool now creates and enters `process.work_dir` before startup
  appliers run. Early-loaded roles and applications now read and write
  relative files in the instance workdir instead of the startup directory.
  Initial configuration validation still runs before directory creation
  (gh-13086).
