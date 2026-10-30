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

-- Manually remove all xlogs on master_quorum2 to break replication to master_quorum1.
fio = require('fio')
for _, path in ipairs(fio.glob(fio.pathjoin(box.cfg.wal_dir, '*.xlog'))) do fio.unlink(path) end

box.space.test:insert{3}

-- Check error reporting.
test_run:cmd("switch master_quorum1")
box.cfg{replication = repl}
box.space.test:select()
other_id = box.info.id % 2 + 1
test_run:wait_upstream(other_id, {status = 'loading', message_re = 'Missing'})
test_run:cmd("switch master_quorum2")
box.space.test:select()
other_id = box.info.id % 2 + 1
test_run:wait_upstream(other_id, {status = 'follow', message_re = box.NULL})
test_run:wait_downstream(other_id, {status = 'stopped', message_re = 'Missing'})
test_run:cmd("switch default")
-- Cleanup.
test_run:drop_cluster(SERVERS)
