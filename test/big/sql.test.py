sql.sort = True

#
# Prepare spaces
#
admin("s = box.schema.create_space('tweedledum', { id = 0 })")
admin("s:create_index('primary', { type = 'hash', parts = { 0, 'str'} })")
admin("s:create_index('secondary', { type = 'tree', unique = false, parts = {1, 'str'}})")

print """#
# A test case for Bug#729758
# "SELECT fails with a disjunct and small LIMIT"
# https://bugs.launchpad.net/tarantool/+bug/729758
#"""

sql("insert into t0 values ('Doe', 'Richard')")
sql("insert into t0 values ('Roe', 'Richard')")
sql("insert into t0 values ('Woe', 'Richard')")
sql("insert into t0 values ('Major', 'Tomas')")
sql("insert into t0 values ('Kytes', 'Tomas')")
sql("select * from t0 where k1='Richard' limit 2")

print """#
# A test case for Bug#729879
# "Zero limit is treated the same as no limit"
# https://bugs.launchpad.net/tarantool/+bug/729879
#"""
sql("select * from t0 where k1='Richard' limit 0")
admin("s:truncate()")

print """#
# A test case for Bug#730593
# "Bad data if incomplete tuple"
# https://bugs.launchpad.net/tarantool/+bug/730593
# Verify that if there is an index on, say, field 2,
# we can't insert tuples with cardinality 1 and
# get away with it.
#"""
sql("insert into t0 values ('Britney')")
sql("select * from t0 where k1='Anything'")
sql("insert into t0 values ('Stephanie')")
sql("select * from t0 where k1='Anything'")
sql("insert into t0 values ('Spears', 'Britney')")
sql("select * from t0 where k0='Spears'")
sql("select * from t0 where k1='Anything'")
sql("select * from t0 where k1='Britney'")
sql("call box.select_range(0, 0, 100, 'Spears')")
sql("call box.select_range(0, 1, 100, 'Britney')")
sql("delete from t0 where k0='Spears'")
# Cleanup
admin("s:truncate()")

print """#
# Test composite keys with trees
#"""
# Redefine the second key to be composite
admin("s.index.secondary:alter{unique = true, parts = { 1, 'str', 2, 'str'}}")

sql("insert into t0 values ('key1', 'part1', 'part2')")
# Test a duplicate insert on unique index that once resulted in a crash (bug #926080)
sql("replace into t0 values ('key1', 'part1', 'part2')")
sql("insert into t0 values ('key2', 'part1', 'part2_a')")
sql("insert into t0 values ('key3', 'part1', 'part2_b')")
admin("s.index[1]:select{}")
sql("select * from t0 where k0='key1'")
sql("select * from t0 where k0='key2'")
sql("select * from t0 where k0='key3'")
sql("select * from t0 where k1='part1'")
sql("call box.select_range(0, 1, 100, 'part1')")
sql("call box.select_range(0, 0, 100, 'key2')")
sql("call box.select_range(0, 1, 100, 'part1', 'part2_a')")
sql("select * from t0 where k0='key1'")
sql("select * from t0 where k0='key2'")
sql("select * from t0 where k0='key3'")
sql("select * from t0 where k1='part1'")
sql("delete from t0 where k0='key1'")
sql("delete from t0 where k0='key2'")
sql("delete from t0 where k0='key3'")
admin("s:truncate()")
# check non-unique multipart keys
admin("s.index.primary:alter{type = 'tree', parts = { 0, 'num'}}")
admin("s.index.secondary:alter{unique = false}")

sql("insert into t0 values (01234567, 'part1', 'part2')")
sql("insert into t0 values (11234567, 'part1', 'part2')")
sql("insert into t0 values (21234567, 'part1', 'part2_a')")
sql("insert into t0 values (31234567, 'part1_a', 'part2')")
sql("insert into t0 values (41234567, 'part1_a', 'part2_a')")
admin("l = {}")
admin("for k, v in s:pairs() do table.insert(l, v) end")
admin("return l")
sql("select * from t0 where k0=01234567")
sql("select * from t0 where k0=11234567")
sql("select * from t0 where k0=21234567")
sql("select * from t0 where k1='part1'")
sql("select * from t0 where k1='part1_a'")
sql("select * from t0 where k1='part_none'")
sql("call box.select(0, 1, 'part1', 'part2')")
sql("select * from t0 where k1='part1'")
sql("select * from t0 where k1='part2'")
# cleanup
sql("delete from t0 where k0=01234567")
sql("delete from t0 where k0=11234567")
sql("delete from t0 where k0=21234567")
sql("delete from t0 where k0=31234567")
sql("delete from t0 where k0=41234567")
admin("s:select{}")
admin("s:truncate()")

admin("s.index.primary:alter{type = 'hash'}")
admin("s.index.secondary:alter{type = 'hash', unique = true, parts = { 1, 'str' }}")

sql("insert into t0 values (1, 'hello')")
sql("insert into t0 values (2, 'brave')")
sql("insert into t0 values (3, 'new')")
sql("insert into t0 values (4, 'world')")
# Check how build_idnexes() works
server.stop()
server.start()
admin("s = box.space[0]")
print """#
# Bug#929654 - secondary hash index is not built with build_indexes()
#"""
sql("select * from t0 where k1='hello'")
sql("select * from t0 where k1='brave'")
sql("select * from t0 where k1='new'")
sql("select * from t0 where k1='world'")
#
admin("s:truncate()")


print """
#
# A test case for: http://bugs.launchpad.net/bugs/735140
# Partial REPLACE corrupts index.
#
"""
# clean data and restart with appropriate config
admin("s.index.primary:alter{parts = {0, 'str'}}")
admin("s.index.secondary:alter{type = 'tree', unique = false}")

sql("insert into t0 values ('Spears', 'Britney')")
sql("select * from t0 where k0='Spears'")
sql("select * from t0 where k1='Britney'")
# try to insert the incoplete tuple
sql("replace into t0 values ('Spears')")
# check that nothing has been updated
sql("select * from t0 where k0='Spears'")
# cleanup
sql("delete from t0 where k0='Spears'")

#
# Test retrieval of duplicates via a secondary key
#
admin("s.index.primary:alter{parts = { 0, 'num'}}")
sql("insert into t0 values (1, 'duplicate one')")
sql("insert into t0 values (2, 'duplicate one')")
sql("insert into t0 values (3, 'duplicate one')")
sql("insert into t0 values (4, 'duplicate one')")
sql("insert into t0 values (5, 'duplicate one')")
sql("insert into t0 values (6, 'duplicate two')")
sql("insert into t0 values (7, 'duplicate two')")
sql("insert into t0 values (8, 'duplicate two')")
sql("insert into t0 values (9, 'duplicate two')")
sql("insert into t0 values (10, 'duplicate two')")
sql("insert into t0 values (11, 'duplicate three')")
sql("insert into t0 values (12, 'duplicate three')")
sql("insert into t0 values (13, 'duplicate three')")
sql("insert into t0 values (14, 'duplicate three')")
sql("insert into t0 values (15, 'duplicate three')")
sql("select * from t0 where k1='duplicate one'")
sql("select * from t0 where k1='duplicate two'")
sql("select * from t0 where k1='duplicate three'")
sql("delete from t0 where k0=1")
sql("delete from t0 where k0=2")
sql("delete from t0 where k0=3")
sql("delete from t0 where k0=4")
sql("delete from t0 where k0=5")
sql("delete from t0 where k0=6")
sql("delete from t0 where k0=7")
sql("delete from t0 where k0=8")
sql("delete from t0 where k0=9")
sql("delete from t0 where k0=10")
sql("delete from t0 where k0=11")
sql("delete from t0 where k0=12")
sql("delete from t0 where k0=13")
sql("delete from t0 where k0=14")
sql("delete from t0 where k0=15")
#
# Check min() and max() functions
#
sql("insert into t0 values(1, 'Aardvark ')")
sql("insert into t0 values(2, 'Bilimbi')")
sql("insert into t0 values(3, 'Creature ')")
admin("s.index[1]:select{}")
admin("s.index[0].idx:min()")
admin("s.index[0].idx:max()")
admin("s.index[1].idx:min()")
admin("s.index[1].idx:max()")
sql("delete from t0 where k0=1")
sql("delete from t0 where k0=2")
sql("delete from t0 where k0=3")
admin("s:drop()")

sql.sort = False
