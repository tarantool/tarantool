## feature/config

* Added the built-in `roles.recovery-point-manager` role. This role
  periodically creates recovery points across a cluster, using a configurable
  backend (the built-in `net-replicaset` backend creates a point on a remote
  replicaset's leader) (gh-12865).
