test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

format = {}
format[1] = {'id', 'integer'}
s = box.schema.create_space('test', {format = format})
box.execute("SELECT * FROM \"test\";")
box.execute("INSERT INTO \"test\" VALUES (1);")
box.execute("DELETE FROM \"test\";")
box.execute("UPDATE \"test\" SET id = 3;")

s:drop()

-- Notorious artefact: check of view referencing counter occurs
-- after drop of indexes. So, if space:drop() fails due to being
-- referenced by a view, space becomes unusable in SQL terms.
--
box.execute("CREATE TABLE t1 (id INT PRIMARY KEY);")
box.execute("CREATE VIEW v1 AS SELECT * FROM t1;")
box.space.T1:drop()
box.execute("SELECT * FROM v1;")
box.space.V1:drop()
box.space.T1:drop()
