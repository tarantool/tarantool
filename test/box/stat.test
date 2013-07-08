# encoding: utf-8
#
# clear statistics
server.restart()

print """#
# check stat_cleanup
#  add several tuples
#
"""
for i in range(10):
  sql("insert into t0 values ({0}, 'tuple')".format(i))
admin("show stat")
print """#
# restart server
#
"""
server.restart()
print """#
# statistics must be zero
#
"""
admin("show stat")

# cleanup
for i in range(10):
  sql("delete from t0 where k0 = {0}".format(i))

# vim: syntax=python
