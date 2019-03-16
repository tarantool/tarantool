env = require('test_run')
test_run = env.new()

box.execute("pragma sql_default_engine='vinyl'")
box.execute("CREATE TABLE t1_vinyl(a INT PRIMARY KEY, b INT, c INT);")
box.execute("CREATE TABLE t2_vinyl(a INT PRIMARY KEY, b INT, c INT);")

box.execute("pragma sql_default_engine='memtx'")
box.execute("CREATE TABLE t3_memtx(a INT PRIMARY KEY, b INT, c INT);")

assert(box.space.T1_VINYL.engine == 'vinyl')
assert(box.space.T2_VINYL.engine == 'vinyl')
assert(box.space.T3_MEMTX.engine == 'memtx')

box.execute("DROP TABLE t1_vinyl;")
box.execute("DROP TABLE t2_vinyl;")
box.execute("DROP TABLE t3_memtx;")
