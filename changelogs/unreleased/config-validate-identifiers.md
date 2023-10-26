## feature/config

* Introduced validation for replicaset_name/uuid and instance_name/uuid
  mismatches before the recovery process when Tarantool is configured via
  a YAML file or etcd.
