# encoding: utf-8
print """# incr/decr big value """
memcached("set bug21 0 0 19\r\n9223372036854775807\r\n")
memcached("incr bug21 1\r\n")
memcached("incr bug21 1\r\n")
memcached("decr bug21 1\r\n")

print """# underflow protection """
memcached("set num 0 0 1\r\n1\r\n")
memcached("incr num 1\r\n")
memcached("incr num 8\r\n")
memcached("decr num 1\r\n")
memcached("decr num 9\r\n")
memcached("decr num 5\r\n")

print """# 32-bit value """
memcached("set num 0 0 10\r\n4294967296\r\n")
memcached("incr num 1\r\n")

print """# overflow value """
memcached("set num 0 0 20\r\n18446744073709551615\r\n")
memcached("incr num 1\r\n")

print """# bogus """
memcached("decr bogus 1\r\n")
memcached("decr incr 1\r\n")

print """# bit increment """
memcached("set bigincr 0 0 1\r\n0\r\n")
memcached("incr num 18446744073709551610\r\n")

print """# incr text value error """
memcached("set text 0 0 2\r\nhi\r\n")
memcached("incr text 1\r\n")

# resore default suite config
server.stop()
server.deploy(self.suite_ini["config"])
# vim: syntax=python
