## bugfix/lua

* Fixed alias detection in the YAML serializer in case the input contains
  objects that implement the `__serialize` meta method (gh-8240).
