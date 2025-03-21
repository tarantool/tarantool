## bugfix/replication

* Fixed a bug when, while checking for a sync quorum, a node would count all
  successfully synced appliers, even those belonging to replicas that are
  booting now (gh-11157).
