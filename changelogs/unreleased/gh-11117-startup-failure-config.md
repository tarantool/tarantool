## feature/config

* Added logging of the collected instance configuration when declarative
  configuration startup fails after the configuration has been collected
  and `TT_CONFIG_DEBUG=1` is set. Sensitive values are masked
  (gh-11117).
