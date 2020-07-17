test_run = require('test_run').new()

--
-- gh-4424 Always enter orphan mode on error in replication
-- configuration change.
--
replication_connect_timeout = box.cfg.replication_connect_timeout
replication_connect_quorum = box.cfg.replication_connect_quorum
box.cfg{replication="12345", replication_connect_timeout=0.1, replication_connect_quorum=1}
box.info.status
box.info.ro
-- reset replication => leave orphan mode
box.cfg{replication=""}
box.info.status
box.info.ro
-- no switch to orphan when quorum == 0
box.cfg{replication="12345", replication_connect_quorum=0}
box.info.status
box.info.ro

-- we could connect to one out of two replicas. Set orphan.
box.cfg{replication_connect_quorum=2}
box.cfg{replication={box.cfg.listen, "12345"}}
box.info.status
box.info.ro
-- lower quorum => leave orphan mode
box.cfg{replication_connect_quorum=1}
test_run:wait_cond(function() return box.info.status == 'running' and box.info.ro == false end)

box.cfg{                                                        \
    replication = {},                                           \
    replication_connect_quorum = replication_connect_quorum,    \
    replication_connect_timeout = replication_connect_timeout   \
}
