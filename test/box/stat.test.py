# encoding: utf-8
#
# clear statistics
server.restart()
admin("box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')")
admin("box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')")

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
