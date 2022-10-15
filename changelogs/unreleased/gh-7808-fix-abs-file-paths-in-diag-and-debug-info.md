## feature/build

* Made diagnostics and debugging information provide absolute file paths
  instead of relative ones when building by default (absolute file paths in
  debugging information can be retained by turning on `DEV_BUILD` CMake
  option) (gh-7808).
