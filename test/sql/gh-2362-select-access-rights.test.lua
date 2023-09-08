test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})
nb = require('net.box')

box.execute("CREATE TABLE t1 (s1 INT PRIMARY KEY, s2 INT UNIQUE);")
box.execute("CREATE TABLE t2 (s1 INT PRIMARY KEY);")
box.execute("INSERT INTO t1 VALUES (1, 1);")
box.execute("INSERT INTO t2 VALUES (1);")

box.schema.user.grant('guest', 'execute', 'sql')
box.schema.user.grant('guest','read', 'space', 't1')
c = nb.connect(box.cfg.listen)
c:execute("SELECT * FROM SEQSCAN t1;")

box.schema.user.revoke('guest','read', 'space', 't1')
c = nb.connect(box.cfg.listen)
c:execute("SELECT * FROM SEQSCAN t1;")

box.schema.user.grant('guest','read', 'space', 't2')
c = nb.connect(box.cfg.listen)
c:execute('SELECT * FROM SEQSCAN t1, SEQSCAN t2 WHERE t1.s1 = t2.s1')

box.execute("CREATE VIEW v AS SELECT * FROM SEQSCAN t1")

box.schema.user.grant('guest','read', 'space', 'v')
v = nb.connect(box.cfg.listen)
c:execute('SELECT * FROM v')

-- Cleanup
box.schema.user.revoke('guest','read','space', 'v')
box.schema.user.revoke('guest','read','space', 't2')
box.schema.user.revoke('guest', 'execute', 'sql')

box.execute('DROP VIEW v')
box.execute('DROP TABLE t2')
box.execute("DROP TABLE t1")
