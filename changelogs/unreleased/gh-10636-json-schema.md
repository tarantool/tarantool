## feature/schema

 * Added `<schema object>:jsonschema()` method to
   `experimental.config.utils.schema` for generating a JSON schema
   representation as a Lua table (gh-10636).

## feature/config

* Added `:jsonschema()` method to the `config` module. This method generates
  and returns the JSON schema of the cluster configuration, providing a detailed
  description of each field (gh-10636).
