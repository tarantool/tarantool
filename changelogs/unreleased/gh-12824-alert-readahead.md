## feature/config

* Added a check for large `readahead` values. When
  `config.checks.readahead` is set to `true` and `readahead` is
  greater than or equal to 1024^2 - 64, a warning alert appears in
  `box.info.config.alerts` (gh-12824).
