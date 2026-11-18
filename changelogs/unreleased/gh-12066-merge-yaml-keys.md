## feature/lua/yaml

* Added support for YAML merge keys (`<<`) in the YAML decoder, allowing maps
  to inherit values from YAML anchors and override / extend them (gh-12066).
