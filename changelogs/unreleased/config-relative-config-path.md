## bugfix/config

* Added support for the relative path in the `--config <...>` option. Before
  this change, `config:reload()` failed if the `process.work_dir` option was
  set to a non-null value (gh-8862).
