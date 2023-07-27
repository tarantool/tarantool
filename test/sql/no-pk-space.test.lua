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
