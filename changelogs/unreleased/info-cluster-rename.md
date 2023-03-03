## bugfix/core

* **[Breaking change]** The table `box.info.cluster` is renamed to
  `box.info.replicaset`. The behaviour can be reverted using the `compat` option
  `box_info_cluster_meaning`
  (https://tarantool.io/compat/box_info_cluster_meaning) (gh-5029).
