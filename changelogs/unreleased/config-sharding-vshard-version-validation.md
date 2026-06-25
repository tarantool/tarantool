## bugfix/config

* An unsupported (too old) or missing vshard version required by the sharding
  configuration is now detected at the configuration validation stage, rather
  than during application. This ensures that invalid configurations are rejected
  before any changes are applied.
