## bugfix/lua

* Fixed memory leaks in `yaml.decode()` on invalid arguments, malformed YAML,
  disallowed non-finite numbers, and NaN mapping keys (gh-10184).
