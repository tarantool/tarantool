import os
import yaml
from os.path import abspath

#
# This test used to pass:
#
# Empty xlog.inprogress must be deleted during recovery
#
# it doesn't pass any more since an xlog with missing header
# can't be parsed by xdir_scan, thus we do nothing about it.
# 
server.stop()
server.deploy()
lsn = str(yaml.load(server.admin("box.info.server.lsn", silent=True))[0])
path = os.path.join(server.vardir, server.name)
filename = os.path.join(path, lsn.zfill(20) + ".xlog")
f = open(filename, "w+")
f.close()
server.start()
server.stop()
if os.access(filename, os.F_OK):
    print ".xlog exists"
# the server has started but is crippled since it
# can't override an existing file
server.start()
server.admin("_ = box.schema.space.create('test')")
os.unlink(filename)
server.admin("_ = box.schema.space.create('test')")
server.admin("box.space.test:drop()")
