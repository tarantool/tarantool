-- gh-2991 - Tarantool asserts on box.cfg.replication update if one of
-- servers is dead
replication_timeout = box.cfg.replication_timeout
replication_connect_timeout = box.cfg.replication_connect_timeout
box.cfg{replication_timeout=0.05, replication_connect_timeout=0.05, replication={}}
box.cfg{replication_connect_quorum=2}
box.cfg{replication = {'127.0.0.1:12345', box.cfg.listen}}
box.info.status
box.info.ro
box.cfg{replication = "", replication_timeout = replication_timeout, \
        replication_connect_timeout = replication_connect_timeout}
