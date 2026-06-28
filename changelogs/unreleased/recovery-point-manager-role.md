## feature/config

* Added the built-in `roles.recovery-point-manager` role. It periodically
  creates recovery points across a cluster from the declarative configuration,
  using a configurable backend (the built-in `net-replicaset` backend creates a
  point on a remote replicaset's leader) (gh-12865).
