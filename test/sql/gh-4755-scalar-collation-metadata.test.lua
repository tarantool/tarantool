env = require('test_run')
test_run = env.new()

--
-- gh-4755: collation in metadata must be displayed for both
-- string and scalar field types.
--
test_run:cmd("setopt delimiter ';'");
box.execute([[update "_session_settings" set "value" = true
              where "name" = 'sql_full_metadata';]]);
box.execute([[CREATE TABLE test (a SCALAR COLLATE "unicode_ci" PRIMARY KEY,
                                 b STRING COLLATE "unicode_ci");]]);
box.execute("SELECT * FROM test;");

--
-- Cleanup.
--
box.execute([[update "_session_settings" set "value" = false
              where "name" = 'sql_full_metadata';]]);
box.execute("DROP TABLE test;");
