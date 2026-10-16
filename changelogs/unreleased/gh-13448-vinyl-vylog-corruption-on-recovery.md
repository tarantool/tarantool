## bugfix/vinyl

* Fixed Vinyl metadata log (vylog) corruption during recovery, which could
  cause `box.snapshot()` to fail with the `Invalid VYLOG file` error and
  prevent the instance from restarting (gh-13448).
