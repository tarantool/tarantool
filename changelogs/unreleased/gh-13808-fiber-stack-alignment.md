## bugfix/core

* Fixed a crash that could occur when a C module started a fiber with a
  custom stack size that was too small for the platform, such as a 16 KiB
  stack on Apple Silicon, which previously passed validation. The C API now
  rejects such stack sizes (gh-13808).
