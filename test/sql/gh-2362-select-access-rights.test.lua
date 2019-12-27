test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})
nb = require('net.box')

box.execute("CREATE TABLE t1 (s1 INT PRIMARY KEY, s2 INT UNIQUE);")
box.execute("CREATE TABLE t2 (s1 INT PRIMARY KEY);")
box.execute("INSERT INTO t1 VALUES (1, 1);")
box.execute("INSERT INTO t2 VALUES (1);")

box.schema.user.grant('guest','read', 'space', 'T1')
c = nb.connect(box.cfg.listen)
c:execute("SELECT * FROM t1;")

box.schema.user.revoke('guest','read', 'space', 'T1')
c = nb.connect(box.cfg.listen)
c:execute("SELECT * FROM t1;")

box.schema.user.grant('guest','read', 'space', 'T2')
c = nb.connect(box.cfg.listen)
c:execute('SELECT * FROM t1, t2 WHERE t1.s1 = t2.s1')

box.execute("CREATE VIEW v AS SELECT * FROM t1")

box.schema.user.grant('guest','read', 'space', 'V')
v = nb.connect(box.cfg.listen)
c:execute('SELECT * FROM v')

box.execute('CREATE TABLE t3 (s1 INT PRIMARY KEY, fk INT, FOREIGN KEY (fk) REFERENCES t1(s2))')
box.schema.user.grant('guest','read','space', 'T3')
v = nb.connect(box.cfg.listen)
c:execute('INSERT INTO t3 VALUES (1, 1)')

-- Cleanup
box.schema.user.revoke('guest','read','space', 'V')
box.schema.user.revoke('guest','read','space', 'T2')
box.schema.user.revoke('guest','read','space', 'T3')

box.execute('DROP VIEW v')
box.execute('DROP TABLE t3')
box.execute('DROP TABLE t2')
box.execute("DROP TABLE t1")
