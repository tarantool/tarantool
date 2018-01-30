test_run = require('test_run').new()

-- create space
box.sql.execute("CREATE TABLE t(id PRIMARY KEY)")

box.sql.execute("CREATE TRIGGER tt_bu BEFORE UPDATE ON t BEGIN SELECT 1; END")
box.sql.execute("CREATE TRIGGER tt_au AFTER UPDATE ON t BEGIN SELECT 1; END")
box.sql.execute("CREATE TRIGGER tt_bi BEFORE INSERT ON t BEGIN SELECT 1; END")
box.sql.execute("CREATE TRIGGER tt_ai AFTER INSERT ON t BEGIN SELECT 1; END")
box.sql.execute("CREATE TRIGGER tt_bd BEFORE DELETE ON t BEGIN SELECT 1; END")
box.sql.execute("CREATE TRIGGER tt_ad AFTER DELETE ON t BEGIN SELECT 1; END")

-- check that these triggers exist
box.sql.execute("SELECT * FROM \"_trigger\"")

-- drop table
box.sql.execute("DROP TABLE t")

-- check that triggers were dropped with deleted table
box.sql.execute("SELECT * FROM \"_trigger\"")
