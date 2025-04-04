## bugfix/config

* Now, `replication.bootstrap_strategy` can't be specified in the instance scope
  since it only makes sense to specify the option for at least for the whole
  replicaset.
