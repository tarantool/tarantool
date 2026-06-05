## feature/config

* Added `config:cluster_config()` to return a raw cluster configuration after
  merging all cluster configuration sources. Default values are not applied, and
  template variables are not substituted in the returned configuration
  (gh-10617).
