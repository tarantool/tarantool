## bugfix/config

* Effectively supported `replication.anon` option, which was broken in several
  ways before (gh-9432).

  There are caveats that are not resolved yet:

  * An anonymous replica shouldn't be used as an upstream for non-anonymous
    replica.
  * An anonymous replica possibly shouldn't be used as an upstream for other
    anonymous replicas by default.
  * An anonymous replica shouldn't be chosen as a bootstrap leader in
    `replication.failover: supervised` mode.
  * An attempt to configure a replicaset where all instances are anonymous
    replicas should lead to an error on config validation, before configuration
    applying.
  * An attempt to configure an anonymous replica in read-write mode (using
    `database.mode` or `<replicaset>.leader`) should lead to an error on config
    validation, before configuration applying.
  * An attempt to configure an anonymous replica with
    `replication.election_mode` != `off` should lead to an error on config
    validation, before configuration applying.
  * An anonymous replica can't be bootstrapped from a replicaset, where all the
    instance are in read-only mode, however there are no technical problems
    there (just too tight validation).
