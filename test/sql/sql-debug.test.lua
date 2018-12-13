remote = require('net.box')
test_run = require('test_run').new()

--
-- gh-3832: Some statements do not return column type

-- Check that "PRAGMA parser_trace" returns 0 or 1 if called
-- without parameter.
result = box.sql.execute('PRAGMA parser_trace')
box.sql.execute('PRAGMA parser_trace = 1')
box.sql.execute('PRAGMA parser_trace')
box.sql.execute('PRAGMA parser_trace = '.. result[1][1])

--
-- Make PRAGMA command return the result as a result set.
--
box.sql.execute('PRAGMA')
