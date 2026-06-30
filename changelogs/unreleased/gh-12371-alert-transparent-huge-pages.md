## feature/config

* Added a check for enabled Transparent Huge Pages (THP). When
  `config.checks.transparent_huge_pages` is set to `true` and THP is
  set to `always` or `madvise` mode, a warning alert appears in
  `box.info.config.alerts` (gh-12371).
