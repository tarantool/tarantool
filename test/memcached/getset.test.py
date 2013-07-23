# encoding: utf-8
print """# set foo (and should get it) """
memcached("set foo 0 0 6\r\nfooval\r\n")
memcached("get foo\r\n")

print """# add bar (and should get it)"""
memcached("set bar 0 0 6\r\nbarval\r\n")
memcached("get bar\r\n")

print """# add foo (but shouldn't get new value)"""
memcached("add foo 0 0 5\r\nfoov2\r\n")
memcached("get foo\r\n")

print """# replace bar (should work)"""
memcached("replace bar 0 0 6\r\nbarva2\r\n")
memcached("get bar\r\n")

print """# replace notexist (shouldn't work)"""
memcached("replace notexist 0 0 6\r\nbarva2\r\n")
memcached("get notexist\r\n")

print """# delete foo"""
memcached("delete foo\r\n")
memcached("get foo\r\n")

print """# delete foo again. not found this time."""
memcached("delete foo\r\n")
memcached("get foo\r\n")

print """# add moo"""
memcached("add moo 0 0 6\r\nmooval\r\n")
memcached("get moo\r\n")

print """# check-and-set (cas) failure case, try to set value with incorrect cas unique val"""
memcached("cas moo 0 0 6 0\r\nMOOVAL\r\n")
memcached("get moo\r\n")

result = memcached("gets moo\r\n", silent=True)
unique_id = int(result.split()[4])

print """# now test that we can store moo with the correct unique id"""
memcached("cas moo 0 0 6 %d\r\nMOOVAL\r\n" % unique_id, silent=True)
memcached("get moo\r\n")

memcached("set foo 0 0 6\r\nfooval\r\ndelete foo\r\nset foo 0 0 6\r\nfooval\r\ndelete foo\r\n")

len = 1024
while len < (1024 * 1028):
    val = 'B' * len
    if len > (1024 * 1024):
        print """# Ensure causing a memory overflow doesn't leave stale data."""
        print "# set small data: - should pass"
        memcached("set foo_%d 0 0 3\r\nMOO\r\n" % (len))
        memcached("get foo_%d\r\n" % (len))
        print "# set big data: - should fail"
        print "set foo_%d 0 0 %d\r\n<big-data>\r\n" % (len, len)
        print memcached("set foo_%d 0 0 %d\r\n%s\r\n" % (len, len, val), silent=True)
    else:
        print "# set big data: - should pass"
        print "set foo_%d 0 0 %d\r\n<big-data>\r\n" % (len, len)
        print memcached("set foo_%d 0 0 %d\r\n%s\r\n" % (len, len, val), silent=True)
    len += 1024 * 512

print """#
# A test for Bug#898198 memcached protocol isn't case-insensitive"
#"""

memcached("SET foo 0 0 6\r\nfooval\r\n")
memcached("GET foo\r\n")
memcached("ADD foo 0 0 5\r\nfoov2\r\n")
memcached("GET foo\r\n")
memcached("REPLACE bar 0 0 6\r\nbarva2\r\n")
memcached("GET bar\r\n")
memcached("DELETE foo\r\n")
memcached("GET foo\r\n")
memcached("CAS moo 0 0 6 0\r\nMOOVAL\r\n")
memcached("GET moo\r\n")
memcached("GETS moo\r\n")

# resore default suite config
server.stop()
server.deploy(self.suite_ini["config"])
# vim: syntax=python
