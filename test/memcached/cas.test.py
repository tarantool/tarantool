# encoding: utf-8
import sys
from lib.memcached_connection import MemcachedConnection

memcached("cas bad blah 0 0 0\r\n")
memcached("cas bad 0 blah 0 0\r\n")
memcached("cas bad 0 0 blah 0\r\n")
memcached("cas bad 0 0 0 blah\r\n")

print """# gets foo (should not exist) """
memcached("gets foo\r\n")

print """# set foo """
memcached("set foo 0 0 6\r\nbarval\r\n")

print """# gets foo and verify identifier exists """
memcached("gets foo\r\n")

print """# cas fail """
memcached("cas foo 0 0 6 123\r\nbarva2\r\n")

print """# gets foo and verify identifier exists """
sys.stdout.write("gets foo\r\n")
result = memcached("gets foo\r\n", silent=True)
unique_id = int(result.split()[4])

print """# cas success """
sys.stdout.write("cas foo 0 0 6 <unique_id>\r\nbarva2\r\n")
result = memcached("cas foo 0 0 6 %d\r\nbarva2\r\n" % unique_id, silent=True)
sys.stdout.write(result)

print """# cas failure (reusing the same key) """
sys.stdout.write("cas foo 0 0 6 <unique_id>\r\nbarva2\r\n")
result = memcached("cas foo 0 0 6 %d\r\nbarva2\r\n" % unique_id, silent=True)
sys.stdout.write(result)

print """# delete foo """
memcached("delete foo\r\n")

print """# cas missing """
sys.stdout.write("cas foo 0 0 6 <unique_id>\r\nbarva2\r\n")
result = memcached("cas foo 0 0 6 %d\r\nbarva2\r\n" % unique_id, silent=True)
sys.stdout.write(result)

print """# set foo1 """
memcached("set foo1 0 0 1\r\n1\r\n")

print """# set foo2 """
memcached("set foo2 0 0 1\r\n2\r\n")

print """# gets foo1 check """
result = memcached("gets foo1\r\n")
foo1_cas = int(result.split()[4])

print """# gets foo2 check """
result = memcached("gets foo2\r\n")
foo2_cas = int(result.split()[4])

print """# validate foo1 != foo2 """
if foo1_cas != foo2_cas:
    print "pass: foo1_cas != foo2_cas"
else:
    print "fail: foo1_cas == foo2_cas"

memcached1 = server.memcached
memcached2 = MemcachedConnection('localhost', server.memcached_port)

print """# gets foo from memcached1 - should success """
result = memcached1("gets foo1\r\n", silent=True)
mem1_cas = int(result.split()[4])

print """# gets foo from memcached2 - should success """
result = memcached2("gets foo1\r\n", silent=True)
mem2_cas = int(result.split()[4])

print """# send 'cas foo1' from memcached1 """
memcached1.send("cas foo1 0 0 6 %d\r\nbarva2\r\n" % mem1_cas, silent=True)
print """# send 'cas foo1' from memcached2 """
memcached2.send("cas foo1 0 0 4 %d\r\npear\r\n" % mem2_cas, silent=True)

print """# recv reply 'cas foo1' from memcached1 """
result = memcached1.recv(silent=True)
mem1_cas_result = result.split()[0]
print """# recv reply 'cas foo1' from memcached2 """
result = memcached2.recv(silent=True)
mem2_cas_result = result.split()[0]

if mem1_cas_result == "STORED" and mem2_cas_result == "EXISTS":
    print "race cas: pass"
elif mem1_cas_result == "EXISTS" and mem2_cas_result == "STORED":
    print "race cas: pass"
else:
    print "race cas: fail"
    print "cas 1 = %s" % mem1_cas_result
    print "cas 2 = %s" % mem2_cas_result

print """# set bug15 """
memcached("set bug15 0 0 1\r\n0\r\n")

print """# Check out the first gets. """
result = memcached("gets bug15\r\n", silent=True)
bug15_cas = int(result.split()[4])

print """# Increment. """
memcached("incr bug15 1\r\n")
print """# Validate a changed CAS. """
result = memcached("gets bug15\r\n", silent=True)
next_bug15_cas = int(result.split()[4])

print """# validate bug15_cas != next_bug15_cas """
if bug15_cas != next_bug15_cas:
    print "pass: bug15_cas != next_bug15_cas"
else:
    print "fail: bug15_cas == next_bug15_cas"

# resore default suite config
server.stop()
server.deploy(self.suite_ini["config"])
# vim: syntax=python
