--
-- gh-3365: display an error in upstream on downstream failure.
-- Create a gap in LSN to cause replica's failure.
-- The goal here is to see same error message on both side.
--
test_run = require('test_run').new()
SERVERS = {'master_quorum1', 'master_quorum2'}
-- Deploy a cluster.
test_run:create_cluster(SERVERS)
test_run:wait_fullmesh(SERVERS)
test_run:cmd("switch master_quorum1")
repl = box.cfg.replication
box.cfg{replication = ""}
test_run:cmd("switch master_quorum2")
box.space.test:insert{1}
box.snapshot()
box.space.test:insert{2}
box.snapshot()
test_run:cmd("switch default")
fio = require('fio')
fio.unlink(fio.pathjoin(fio.abspath("."), string.format('master_quorum2/%020d.xlog', 5)))
test_run:cmd("switch master_quorum1")
box.cfg{replication = repl}
require('fiber').sleep(0.1)
box.space.test:select()
other_id = box.info.id % 2 + 1
box.info.replication[other_id].upstream.status
box.info.replication[other_id].upstream.message:match("Missing")
test_run:cmd("switch master_quorum2")
box.space.test:select()
other_id = box.info.id % 2 + 1
box.info.replication[other_id].upstream.status
box.info.replication[other_id].upstream.message
box.info.replication[other_id].downstream.status
box.info.replication[other_id].downstream.message:match("Missing")
test_run:cmd("switch default")
-- Cleanup.
test_run:drop_cluster(SERVERS)
