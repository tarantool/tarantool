# encoding: utf-8
flags_list = [ 0x0, 0x7b, 0xffff ]

for flags in flags_list:
    memcached("set foo %d 0 6\r\nfooval\r\n" % flags)
    result = memcached("gets foo\r\n")
    ret_flags = int(result.split()[2])
    if flags == ret_flags:
        print "success: flags (0x%x) == ret_flags (0x%x)" % (flags, ret_flags)
    else:
        print "fail: flags (0x%x) != ret_flags (0x%x)" % (flags, ret_flags)

# resore default suite config
server.stop()
server.deploy(self.suite_ini["config"])
# vim: syntax=python
