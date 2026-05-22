## feature/config

* Added support for scalar values in `config.context`: a template variable
  can now be set to a string, a number, or a boolean directly.
  Variables can be overridden at any hierarchy level (global,
  group, replicaset, instance) (gh-11002).
