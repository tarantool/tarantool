## bugfix/config

* An unavailable or outdated vshard module for a configured sharding role,
  as well as a configured sharding option that requires a newer vshard, is
  now reported before the first `box.cfg()` call, so a misconfigured instance
  fails fast instead of failing after a potentially long database recovery.
