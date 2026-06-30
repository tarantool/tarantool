## bugfix/core

* Fixed an assertion failure in `fiber_signal_reset()` that occurred during
  application threads initialization on CentOS 7 based systems (gh-12846).
