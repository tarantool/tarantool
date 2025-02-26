## bugfix/replication

* Fixed a bug where when checking for a sync quorum, a node would count all
  successfully synced applications, even those belonging to replicas that are
  booting now (gh-11157).
