-- sql: assertion fault on VALUES #3888
--
-- Make sure that tokens representing values of integer, float,
-- and blob constants are different from tokens representing
-- keywords of the same names.
--
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.sql.execute('pragma sql_default_engine=\''..engine..'\'')

-- check 'VALUES' against typedef keywords (should fail)
box.sql.execute('VALUES(blob)')
box.sql.execute('VALUES(float)')

-- check 'SELECT' against typedef keywords (should fail)
box.sql.execute('SELECT blob')
box.sql.execute('SELECT float')

-- check 'VALUES' against ID (should fail)
box.sql.execute('VALUES(TheColumnName)')

-- check 'SELECT' against ID (should fail)
box.sql.execute('SELECT TheColumnName')

-- check 'VALUES' well-formed expression  (returns value)
box.sql.execute('VALUES(-0.5e-2)')
box.sql.execute('SELECT X\'507265766564\'')

-- check 'SELECT' well-formed expression  (return value)
box.sql.execute('SELECT 3.14')
box.sql.execute('SELECT X\'4D6564766564\'')


