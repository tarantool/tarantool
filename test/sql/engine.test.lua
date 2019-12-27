env = require('test_run')
test_run = env.new()

box.space._session_settings:update('sql_default_engine', {{'=', 2, 'vinyl'}})
box.execute("CREATE TABLE t1_vinyl(a INT PRIMARY KEY, b INT, c INT);")
box.execute("CREATE TABLE t2_vinyl(a INT PRIMARY KEY, b INT, c INT);")

box.space._session_settings:update('sql_default_engine', {{'=', 2, 'memtx'}})
box.execute("CREATE TABLE t3_memtx(a INT PRIMARY KEY, b INT, c INT);")

assert(box.space.T1_VINYL.engine == 'vinyl')
assert(box.space.T2_VINYL.engine == 'vinyl')
assert(box.space.T3_MEMTX.engine == 'memtx')

box.execute("DROP TABLE t1_vinyl;")
box.execute("DROP TABLE t2_vinyl;")
box.execute("DROP TABLE t3_memtx;")

-- gh-4422: allow to specify engine in CREATE TABLE statement.
--
box.execute("CREATE TABLE t1_vinyl (id INT PRIMARY KEY) WITH ENGINE = 'vinyl'")
assert(box.space.T1_VINYL.engine == 'vinyl')
box.execute("CREATE TABLE t1_memtx (id INT PRIMARY KEY) WITH ENGINE = 'memtx'")
assert(box.space.T1_MEMTX.engine == 'memtx')
box.space._session_settings:update('sql_default_engine', {{'=', 2, 'vinyl'}})
box.execute("CREATE TABLE t2_vinyl (id INT PRIMARY KEY) WITH ENGINE = 'vinyl'")
assert(box.space.T2_VINYL.engine == 'vinyl')
box.execute("CREATE TABLE t2_memtx (id INT PRIMARY KEY) WITH ENGINE = 'memtx'")
assert(box.space.T2_MEMTX.engine == 'memtx')

box.space.T1_VINYL:drop()
box.space.T1_MEMTX:drop()
box.space.T2_VINYL:drop()
box.space.T2_MEMTX:drop()

-- Name of engine considered to be string literal, so should be
-- lowercased and quoted.
--
box.execute("CREATE TABLE t1_vinyl (id INT PRIMARY KEY) WITH ENGINE = VINYL")
box.execute("CREATE TABLE t1_vinyl (id INT PRIMARY KEY) WITH ENGINE = vinyl")
box.execute("CREATE TABLE t1_vinyl (id INT PRIMARY KEY) WITH ENGINE = 'VINYL'")
box.execute("CREATE TABLE t1_vinyl (id INT PRIMARY KEY) WITH ENGINE = \"vinyl\"")

-- Make sure that wrong engine name is handled properly.
--
box.execute("CREATE TABLE t_wrong_engine (id INT PRIMARY KEY) WITH ENGINE = 'abc'")
box.execute("CREATE TABLE t_long_engine_name (id INT PRIMARY KEY) WITH ENGINE = 'very_long_engine_name'")
