# encoding: utf-8
#
# clear statistics
server.restart()
admin("space = box.schema.create_space('tweedledum', { id = 0 })")
admin("space:create_index('primary', 'hash', { parts = { 0, 'num' }})")

print """#
# check stat_cleanup
#  add several tuples
#
"""
for i in range(10):
  sql("insert into t0 values ({0}, 'tuple')".format(i))
admin("box.stat()")
print """#
# restart server
#
"""
server.restart()
print """#
# statistics must be zero
#
"""
admin("box.stat()")

# cleanup
admin("box.space[0]:drop()")

# vim: syntax=python
