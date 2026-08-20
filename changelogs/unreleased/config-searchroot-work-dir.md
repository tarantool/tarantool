## bugfix/config

* Modules installed into the configured `process.work_dir` (for example,
  into its `.rocks` directory) are now resolvable before the first
  `box.cfg()` call. In particular, roles and an application marked with
  the `early_load` tag can now be loaded from the working directory.
