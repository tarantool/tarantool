## feature/replication

* Now `box.cfg({bootstrap_strategy = 'supervised'})` without upstreams waits
  for the `box.ctl.make_bootstrap_leader(<...>)` command till the replication
  connect timeout instead of an immediate fail.
