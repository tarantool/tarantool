## bugfix/config

* An unavailable or too old vshard module for a configured sharding role is
  now reported before `box.cfg()`, so a misconfigured instance fails fast
  instead of failing after a potentially long database recovery.
