## feature/config

* Added support for scalar values in `config.context`: a template
  variable can now be set directly to a string, number, or boolean.
  Variables can be overridden at any hierarchy level (global,
  group, replicaset, instance) (gh-11002).
