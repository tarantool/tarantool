## bugfix/config

* Support parent directories creation for options that accept a directory or a
  file (gh-8862).
* Create parent directories for `console.socket` and `log.file` (gh-8862).
* Create the `process.work_dir` directory (gh-8862).
* Consider all the paths as relative to `process.work_dir` when creating
  necessary directories (gh-8862).
