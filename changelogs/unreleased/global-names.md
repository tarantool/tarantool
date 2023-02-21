## feature/replication

* A new option `box.cfg.cluster_name` allows assigning a human-readable name to
  the entire cluster. It has to match in all instances and is displayed in
  `box.info.cluster.name` (gh-5029).

* A new option `box.cfg.replicaset_name` allows assigning a human-readable name
  to the replicaset. It works the same as `box.cfg.replicaset_uuid`. Its value
  must be the same across all instances of one replicaset. The replicaset name
  is displayed in `box.info.replicaset.name` (gh-5029).

* A new option `box.cfg.instance_name` allows assigning a human-readable name to
  the instance. It works the same as `box.cfg.instance_uuid`. Its value must be
  unique in the replicaset. The instance name is displayed in `box.info.name`.
  Names of other replicas in the same replicaset are visible in
  `box.info.replication[id].name` (gh-5029).

* Instance at rebootstrap can change its UUID while keeping its numeric ID if it
  has the same non-empty instance name (gh-5029).
