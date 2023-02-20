## feature/replication

* Introduced the new configuration option `bootstrap_strategy`. The default
  value of this option - "auto" - brings the new behavior of replica on:
    * replica set bootstrap
    * replica join to an existing replica set
    * recovery
    * replication reconfiguration.

  To return to the old behavior, set the option to "legacy".

  The new value "auto" will be in effect only when no value for
  `replication_connect_quorum` is passed. If a value is present,
  `bootstrap_strategy` is automatically set to "legacy" to preserve backward
  compatibility.

  Note that if you leave the options untouched (that is, `bootstrap_strategy`
  defaults to "auto"), the following behavior will noticeably change: during
  the recovery from local files and during replication reconfiguration
  `box.cfg{replication = ...}` will not fail even if some (or all) of the remote
  peers listed in `box.cfg.replication` are unavailable. Instead, the node will
  try to connect to them for a period of `replication_connect_timeout` and then
  transition to `box.info.status == "running"` as soon as it syncs with all the
  reached peers (gh-5272).

* **[Breaking change]** Joining a new replica to a working replica set.
  If neither of the configuration options `bootstrap_strategy` and
  `replication_connect_quorum` is passed explicitly, or if `bootstrap_strategy`
  is set to "auto", bootstrapping a new replica in an existing replica set will
  only succeed if all the replica set members are listed in the replica's
  `box.cfg.replication`. For example, when joining a fresh replica to a replica
  set of 3 nodes, all 3 node URIs must be present in the replica's
  `box.cfg.replication` parameter. When joining 2 new replicas to a single
  master, both replicas must have each other's URIs (alongside with master's
  URI) in their `box.cfg.replication` (gh-5272).
