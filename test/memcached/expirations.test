# encoding: utf-8
import time
import yaml

###################################
def wait_for_next_lsn(lsn, serv):
    serv_admin = serv.admin
    while True:
        if get_lsn(serv) != lsn:
            return lsn
        time.sleep(0.01)

def get_lsn(serv):
    serv_admin = serv.admin
    resp = serv_admin("box.info.lsn", silent=True)
    return yaml.load(resp)[0]

def wait(serv = server):
    lsn = get_lsn(serv)
    return wait_for_next_lsn(lsn, serv)
###################################

print """# expire: after 1 second"""

print """# set foo"""
memcached("set foo 0 1 6\r\nfooval\r\n")

print """# foo shoud be exists"""
memcached("get foo\r\n")

wait()
print """# foo shoud expired"""
memcached("get foo\r\n")


print """# expire: time - 1 second"""
expire = time.time() - 1

print """# set foo"""
memcached("set foo 0 %d 6\r\nfooval\r\n" % expire, silent=True)

print """# foo shoud expired"""
memcached("get foo\r\n")


print """# expire: time + 1 second"""
expire = time.time() + 1

print """# set foo"""
memcached("set foo 0 %d 6\r\nfooval\r\n" % expire, silent=True)

print """# foo shoud be exists"""
memcached("get foo\r\n")

wait()
print """# foo shoud expired"""
memcached("get foo\r\n")

print """# expire: time - 20 second"""
expire = time.time() - 20

print """# set boo"""
memcached("set boo 0 %d 6\r\nbooval\r\n" % expire, silent=True)

print """# foo shoud expired"""
memcached("get boo\r\n")

print """# expire: after 2 second"""

print """# add add"""
memcached("add add 0 1 6\r\naddval\r\n")

print """# readd add - shoud be fail"""
memcached("add add 0 1 7\r\naddval1\r\n")

wait()
print """# readd add - shoud be success"""
memcached("add add 0 1 7\r\naddval2\r\n")

# resore default suite config
server.stop()
server.deploy(self.suite_ini["config"])
# vim: syntax=python
