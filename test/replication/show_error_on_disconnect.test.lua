--
-- gh-3365: display an error in upstream on downstream failure.
-- Create a gap in LSN to cause replica's failure.
-- The goal here is to see same error message on both side.
--
test_run = require('test_run').new()
SERVERS = {'show_error_on_disconnect1', 'show_error_on_disconnect2'}

-- Deploy a cluster.
test_run:init_cluster(SERVERS, "replication", {args="0.1"})
test_run:cmd("switch show_error_on_disconnect1")
repl = box.cfg.replication
box.cfg{replication = ""}
test_run:cmd("switch show_error_on_disconnect2")
box.space.test:insert{1}
box.snapshot()
box.space.test:insert{2}
box.snapshot()

-- Manually remove all xlogs on show_error_on_disconnect2 to break replication to show_error_on_disconnect1.
fio = require('fio')
for _, path in ipairs(fio.glob(fio.pathjoin(box.cfg.wal_dir, '*.xlog'))) do fio.unlink(path) end

box.space.test:insert{3}

-- Check error reporting.
test_run:cmd("switch show_error_on_disconnect1")
box.cfg{replication = repl}
require('fiber').sleep(0.1)
box.space.test:select()
other_id = box.info.id % 2 + 1
test_run:wait_cond(function() return box.info.replication[other_id].upstream.status == 'stopped' end) or box.info.replication[other_id].upstream.status
box.info.replication[other_id].upstream.message:match("Missing")
test_run:cmd("switch show_error_on_disconnect2")
box.space.test:select()
other_id = box.info.id % 2 + 1
test_run:wait_cond(function() return box.info.replication[other_id].upstream.status == 'follow' end) or box.info.replication[other_id].upstream.status
box.info.replication[other_id].upstream.message
test_run:wait_cond(function() return box.info.replication[other_id].downstream.status == 'stopped' end) or box.info.replication[other_id].downstream.status
box.info.replication[other_id].downstream.message:match("Missing")
test_run:cmd("switch default")
-- Cleanup.
test_run:drop_cluster(SERVERS)
