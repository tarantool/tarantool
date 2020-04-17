env = require('test_run')
test_run = env.new()

--
-- gh-4755: collation in metadata must be displayed for both
-- string and scalar field types.
--
test_run:cmd("setopt delimiter ';'");
box.execute([[SET SESSION "sql_full_metadata" = true;]]);
box.execute([[CREATE TABLE test (a SCALAR COLLATE "unicode_ci" PRIMARY KEY,
                                 b STRING COLLATE "unicode_ci");]]);
box.execute("SELECT * FROM test;");

--
-- Cleanup.
--
box.execute([[SET SESSION "sql_full_metadata" = false;]]);
box.execute("DROP TABLE test;");
