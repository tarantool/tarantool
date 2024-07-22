## bugfix/vinyl

* Fixed a multi-threading race condition that could cause a writer thread to
  crash while looking up a tuple format (gh-10278).
