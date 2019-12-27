test_run = require('test_run').new()
engine = test_run:get_cfg('engine')
_ = box.space._session_settings:update('sql_default_engine', {{'=', 2, engine}})

-- gh-3735: make sure that integer overflows errors are
-- handled during VDBE execution.
-- gh-3810: range of integer is extended up to 2^64 - 1.
--
box.execute('SELECT (2147483647 * 2147483647 * 2147483647);')
box.execute('SELECT (-9223372036854775808 / -1);')
box.execute('SELECT (-9223372036854775808 - 1);')
box.execute('SELECT (9223372036854775807 + 1);')
box.execute('SELECT (9223372036854775807 + 9223372036854775807 + 2);')
box.execute('SELECT 18446744073709551615 * 2;')
box.execute('SELECT (-9223372036854775807 * (-2));')

-- Literals are checked right after parsing.
--
box.execute('SELECT 9223372036854775808;')
box.execute('SELECT -9223372036854775809;')
box.execute('SELECT 9223372036854775808 - 1;')
box.execute('SELECT 18446744073709551615;')
box.execute('SELECT 18446744073709551616;')

-- Test that CAST may also leads to overflow.
--
box.execute('SELECT CAST(\'9223372036854775808\' AS INTEGER);')
box.execute('SELECT CAST(\'18446744073709551616\' AS INTEGER);')
-- Due to inexact represantation of large integers in terms of
-- floating point numbers, numerics with value < UINT64_MAX
-- have UINT64_MAX + 1 value in integer representation:
-- float 18446744073709551600 -> int (18446744073709551616),
-- with error due to conversion = 16.
--
box.execute('SELECT CAST(18446744073709551600. AS INTEGER);')
-- gh-3810: make sure that if space contains integers in range
-- [INT64_MAX, UINT64_MAX], they are handled inside SQL in a
-- proper way, which now means that an error is raised.
--
box.execute('CREATE TABLE t (id INT PRIMARY KEY);')
box.space.T:insert({9223372036854775809})
box.space.T:insert({18446744073709551615ULL})
box.execute('SELECT * FROM t;')
box.space.T:drop()

-- Make sure that integers stored in NUMBER field are converted
-- to floating point properly.
--
box.execute("CREATE TABLE t(id INT PRIMARY KEY, a NUMBER);")
box.space.T:insert({1, 18446744073709551615ULL})
box.space.T:insert({2, -1})
box.execute("SELECT * FROM t;")
box.space.T:drop()
