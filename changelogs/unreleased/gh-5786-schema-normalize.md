## feature/config

* The `experimental.config.utils.schema` module now provides the
  `:normalize()` method. It converts human-readable byte sizes and duration
  strings, such as `256MiB` or `30s`, to numeric values according to the
  schema annotations (gh-5786).
