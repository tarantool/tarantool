test_run = require('test_run').new()

-- create space
box.sql.execute("DROP TABLE IF EXISTS t")
box.sql.execute("CREATE TABLE t(id PRIMARY KEY)")

-- create before update trigger
box.sql.execute("CREATE TRIGGER tt_bu BEFORE UPDATE ON t BEGIN SELECT 1; END")

-- create after update trigger
box.sql.execute("CREATE TRIGGER tt_au AFTER UPDATE ON t BEGIN SELECT 1; END")

-- create before insert trigger
box.sql.execute("CREATE TRIGGER tt_bi BEFORE INSERT ON t BEGIN SELECT 1; END")

-- create after insert trigger
box.sql.execute("CREATE TRIGGER tt_ai AFTER INSERT ON t BEGIN SELECT 1; END")

-- create before delete trigger
box.sql.execute("CREATE TRIGGER tt_bd BEFORE DELETE ON t BEGIN SELECT 1; END")

-- create after delete trigger
box.sql.execute("CREATE TRIGGER tt_ad AFTER DELETE ON t BEGIN SELECT 1; END")

-- check that this triggers exist
box.sql.execute("SELECT * FROM _trigger")

-- drop table
box.sql.execute("DROP TABLE t IF EXISTS")

-- check that triggers were dropped with deleted table
box.sql.execute("SELECT * FROM _trigger")
