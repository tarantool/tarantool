import os
import shutil
import yaml
import re

from os.path import abspath

# cleanup server.vardir
server.stop()
server.deploy()
lsn = yaml.load(server.admin("next(box.info.cluster)", silent=True))[1]
server.stop()

print """
# Inprogress xlog must be renamed before second insert.
"""
filename = str(lsn).zfill(20) + ".xlog"
wal_inprogress = os.path.join(server.vardir, filename + ".inprogress")
wal = os.path.join(server.vardir, filename)

server.start()

server.admin("space = box.schema.create_space('tweedledum', { id = 0 })")
if os.access(wal_inprogress, os.F_OK):
  print ".xlog.inprogress exists"

server.admin("space:create_index('primary', { type = 'hash' })")

if os.access(wal, os.F_OK) and not os.access(wal_inprogress, os.F_OK):
  print ".xlog.inprogress has been successfully renamed"
server.stop()
lsn += 2

print """
# Inprogress xlog must be renamed during regular termination.
"""
filename = str(lsn).zfill(20) + ".xlog"
server.start()

wal_inprogress = os.path.join(server.vardir, filename + ".inprogress")
wal = os.path.join(server.vardir, filename)

server.admin("box.space[0]:insert{3, 'third tuple'}")

if os.access(wal_inprogress, os.F_OK):
  print ".xlog.inprogress exists"

server.stop()

if os.access(wal, os.F_OK) and not os.access(wal_inprogress, os.F_OK):
  print ".xlog.inprogress has been successfully renamed"
lsn += 1

print """
# An inprogress xlog file with one record must be renamed during recovery.
"""

server.start()
filename = str(lsn).zfill(20) + ".xlog"
wal_inprogress = os.path.join(server.vardir, filename + ".inprogress")
wal = os.path.join(server.vardir, filename)
server.admin("box.space[0]:insert{4, 'fourth tuple'}")
server.admin("box.space[0]:insert{5, 'Unfinished record'}")
pid = int(yaml.load(server.admin("box.info.pid", silent=True))[0])
from signal import SIGKILL
if pid > 0:
    os.kill(pid, SIGKILL)
server.stop()

if os.access(wal, os.F_OK):
    print ".xlog exists"
    # Remove last byte from xlog
    f = open(wal, "a")
    size = f.tell()
    f.truncate(size - 1)
    f.close()
    os.rename(wal, wal_inprogress)

server.start()

if os.access(wal, os.F_OK) and not os.access(wal_inprogress, os.F_OK):
  print ".xlog.inprogress hash been successfully renamed"
server.stop()
lsn += 1

# print """
# # Empty (zero size) inprogress xlog must be deleted during recovery.
# """
#
# wal_inprogress = os.path.join(server.vardir, "00000000000000000006.xlog.inprogress")
# wal = os.path.join(server.vardir, "00000000000000000006.xlog")
# 
# os.symlink(abspath("box/empty.xlog"), wal_inprogress)
# server.start()
#
# if not os.access(wal_inprogress, os.F_OK) and not os.access(wal, os.F_OK):
#    print "00000000000000000006.xlog.inprogress has been successfully deleted"
# server.stop()

# print """
# # Empty (header only, no records) inprogress xlog must be deleted
# # during recovery.
# """
# 
# # If the previous test has failed, there is a dangling link
# # and symlink fails.
# try:
#   os.symlink(abspath("box/just_header.xlog"), wal_inprogress)
# except OSError as e:
#   print e
# 
# server.start()
# 
# if not os.access(wal_inprogress, os.F_OK) and not os.access(wal, os.F_OK):
#    print "00000000000000000006.xlog.inprogress has been successfully deleted"
# server.stop()

# print """
# # Inprogress xlog with bad record must be deleted during recovery.
# """
# 
# # If the previous test has failed, there is a dangling link
# # and symlink fails.
# try:
#   os.symlink(abspath("box/bad_record.xlog"), wal_inprogress)
# except OSError as e:
#   print e
# 
# server.start()
# 
# if not os.access(wal_inprogress, os.F_OK) and not os.access(wal, os.F_OK):
#    print "00000000000000000006.xlog.inprogress has been successfully deleted"

#print """
#A test case for https://bugs.launchpad.net/tarantool/+bug/1052018
#panic_on_wal_error doesn't work for duplicate key errors
#"""

server.stop()
server.cfgfile_source = "box/panic_on_wal_error.cfg"
server.deploy()
lsn = yaml.load(server.admin("next(box.info.cluster)", silent=True))[1]
filename = str(lsn).zfill(20) + ".xlog"
wal_old = os.path.join(server.vardir, "old_" + filename)
wal = os.path.join(server.vardir, filename)

# Create wal#1
server.admin("space = box.schema.create_space('test')")
server.admin("box.space['test']:create_index('primary')")
server.admin("box.space['test']:insert{1, 'first tuple'}")
server.admin("box.space['test']:insert{2, 'second tuple'}")
server.stop()

# Save wal #1
if os.access(wal, os.F_OK):
    print ".xlog exists"
    os.rename(wal, wal_old)

lsn += 4

# Create another wal#1
server.start()
server.admin("space = box.schema.create_space('test')")
server.admin("box.space['test']:create_index('primary')")
server.admin("box.space['test']:insert{1, 'first tuple'}")
server.admin("box.space['test']:delete{1}")
server.stop()

# Create wal#2
server.start()
server.admin("box.space['test']:insert{1, 'third tuple'}")
server.admin("box.space['test']:insert{2, 'fourth tuple'}")
server.stop()

if os.access(wal, os.F_OK):
    print ".xlog exists"
    # Replace wal#1 with saved copy
    os.unlink(wal)
    os.rename(wal_old, wal)

f = open(server.logfile, "r")
f.seek(0, 2)

server.start()

check="Duplicate key"
print "check log line for '%s'" % check
print
line = f.readline()
while line:
    if re.search(r'(%s)' % check, line):
        print "'%s' exists in server log" % check
        break
    line = f.readline()
print

server.admin("box.space['test']:get{1}")
server.admin("box.space['test']:get{2}")
server.admin("box.space['test']:len()")

# cleanup
server.stop()
server.deploy()
