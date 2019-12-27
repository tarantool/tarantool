-- Regression test for gh-2483
env = require('test_run')
test_run = env.new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

box.schema.user.grant('guest', 'read,write,execute', 'universe')

-- Create a table and insert a datum
box.execute([[CREATE TABLE t(id int PRIMARY KEY)]])
box.execute([[INSERT INTO t (id) VALUES (1)]])

-- Sanity check
box.execute([[SELECT * FROM t]])

test_run:cmd('restart server default');

-- Connect to ourself
c = require('net.box').connect(os.getenv("LISTEN"))

-- This segfaults due to gh-2483 since
-- before the patch sql schema was read on-demand.
-- Which could obviously lead to access denied error.
c:eval([[ return box.execute('SELECT * FROM t') ]])
-- sql.execute([[SELECT * FROM t]])

box.execute([[DROP TABLE t]])
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
