## feature/config

* Now Tarantool instances configured in the supervised failover mode try to
  avoid choosing learner instances as a bootstrap leader. If Tarantool has to
  choose a learner as a bootstrap leader since there is no other options a
  configuration alert is issued (gh-10842).
