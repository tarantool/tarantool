## bugfix/lua/json

* A new `compat` option `json_escape_forward_slash` was added. This option
  configures whether the internal JSON encoder escapes the forward slash
  character (old behavior) or not (new behavior). This option affects the
  `json.encode()` Lua function and the JSON logger (gh-6200).
