test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
box.execute('pragma sql_default_engine=\''..engine..'\'')

box.cfg{}

box.execute("select (9223372036854775807)")
box.execute("select (-9223372036854775808)")

box.execute("select (9223372036854775808)")
box.execute("select (-9223372036854775809)")
