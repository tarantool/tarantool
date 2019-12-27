-- sql: assertion fault on VALUES #3888
--
-- Make sure that tokens representing values of integer, float,
-- and blob constants are different from tokens representing
-- keywords of the same names.
--
test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- check 'VALUES' against typedef keywords (should fail)
box.execute('VALUES(scalar)')
box.execute('VALUES(float)')

-- check 'SELECT' against typedef keywords (should fail)
box.execute('SELECT scalar')
box.execute('SELECT float')

-- check 'VALUES' against ID (should fail)
box.execute('VALUES(TheColumnName)')

-- check 'SELECT' against ID (should fail)
box.execute('SELECT TheColumnName')

-- check 'VALUES' well-formed expression  (returns value)
box.execute('VALUES(-0.5e-2)')
box.execute('SELECT X\'507265766564\'')

-- check 'SELECT' well-formed expression  (return value)
box.execute('SELECT 3.14')
box.execute('SELECT X\'4D6564766564\'')


