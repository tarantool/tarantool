# encoding: utf-8
import sys

print """# Test that commands can take 'noreply' parameter. """
memcached("flush_all noreply\r\n")
memcached("flush_all 0 noreply\r\n")

memcached("get noreply:foo\r\n")
memcached("add noreply:foo 0 0 1 noreply\r\n1\r\n")
memcached("get noreply:foo\r\n")

memcached("set noreply:foo 0 0 1 noreply\r\n2\r\n")
memcached("get noreply:foo\r\n")

memcached("replace noreply:foo 0 0 1 noreply\r\n3\r\n")
memcached("get noreply:foo\r\n")

memcached("append noreply:foo 0 0 1 noreply\r\n4\r\n")
memcached("get noreply:foo\r\n")

memcached("prepend noreply:foo 0 0 1 noreply\r\n5\r\n")
memcached("get noreply:foo\r\n")

sys.stdout.write("gets noreply:foo\r\n")
result = memcached("gets noreply:foo\r\n", silent=True)
unique_id = int(result.split()[4])

sys.stdout.write("cas noreply:foo 0 0 1 <unique_id> noreply\r\n6\r\n")
memcached("cas noreply:foo 0 0 1 %d noreply\r\n6\r\n" % unique_id, silent=True)
memcached("get noreply:foo\r\n")

memcached("incr noreply:foo 3 noreply\r\n")
memcached("get noreply:foo\r\n")

memcached("decr noreply:foo 2 noreply\r\n")
memcached("get noreply:foo\r\n")

memcached("delete noreply:foo noreply\r\n")
memcached("get noreply:foo\r\n")

# resore default suite config
server.stop()
server.deploy(self.suite_ini["config"])
# vim: syntax=python
