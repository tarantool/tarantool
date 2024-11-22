## bugfix/vinyl

* Fixed a bug when joining a new replica to a master instance that experiences
  a heavy write load would severely degrade the master instance performance.
  The fix should also speed up long-running scan requests (gh-10846).
