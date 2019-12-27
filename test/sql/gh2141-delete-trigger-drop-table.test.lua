test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- create space
box.execute("CREATE TABLE t(id INT PRIMARY KEY)")

box.execute("CREATE TRIGGER tt_bu BEFORE UPDATE ON t FOR EACH ROW BEGIN SELECT 1; END")
box.execute("CREATE TRIGGER tt_au AFTER UPDATE ON t FOR EACH ROW BEGIN SELECT 1; END")
box.execute("CREATE TRIGGER tt_bi BEFORE INSERT ON t FOR EACH ROW BEGIN SELECT 1; END")
box.execute("CREATE TRIGGER tt_ai AFTER INSERT ON t FOR EACH ROW BEGIN SELECT 1; END")
box.execute("CREATE TRIGGER tt_bd BEFORE DELETE ON t FOR EACH ROW BEGIN SELECT 1; END")
box.execute("CREATE TRIGGER tt_ad AFTER DELETE ON t FOR EACH ROW BEGIN SELECT 1; END")

-- check that these triggers exist
box.execute("SELECT \"name\", \"opts\" FROM \"_trigger\"")

-- drop table
box.execute("DROP TABLE t")

-- check that triggers were dropped with deleted table
box.execute("SELECT \"name\", \"opts\" FROM \"_trigger\"")
