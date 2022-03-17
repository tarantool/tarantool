## feature/build

* Added bundling of _GNU libunwind_ to support backtrace feature on
  _AARCH64_ architecture and distributives that don't provide _libunwind_
  package.
* Re-enabled backtrace feature for all _RHEL_ distributions by default, except
  for _AARCH64_ architecture and ancient _GCC_ versions, which lack compiler
  features required for backtrace (gh-4611).
