replication_connect_timeout = box.cfg.replication_connect_timeout
replication_connect_quorum = box.cfg.replication_connect_quorum
box.cfg{replication="12345", replication_connect_timeout=0.1, replication_connect_quorum=1}

--
-- gh-3760: replication quorum 0 on reconfiguration should return
-- from box.cfg immediately.
--
replication = box.cfg.replication
box.cfg{                                                        \
    replication = {},                                           \
    replication_connect_quorum = 0,                             \
    replication_connect_timeout = 1000000                       \
}
-- The call below would hang, if quorum 0 is ignored, or checked
-- too late.
box.cfg{replication = {'localhost:12345'}}
box.info.status
box.cfg{                                                        \
    replication = {},                                           \
    replication_connect_quorum = replication_connect_quorum,    \
    replication_connect_timeout = replication_connect_timeout   \
}
