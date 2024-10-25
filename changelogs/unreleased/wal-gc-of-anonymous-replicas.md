## feature/replication

* Now anonymous replicas retain required xlogs. Outdated anonymous replicas
  and their WAL GC state are deleted automatically when being disconnected
  for `box.cfg.replication_anon_ttl` seconds (gh-10755).
