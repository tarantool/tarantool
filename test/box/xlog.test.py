import os
import shutil

from os.path import abspath

# cleanup server.vardir
server.stop()
server.deploy()
server.stop()

print """
# Inprogress xlog must be renamed before second insert.
"""
wal_inprogress = os.path.join(server.vardir, "00000000000000000002.xlog.inprogress")
wal = os.path.join(server.vardir, "00000000000000000002.xlog")

server.start()

admin("space = box.schema.create_space('tweedledum', { id = 0 })")
if os.access(wal_inprogress, os.F_OK):
  print "00000000000000000002.xlog.inprogress exists"

admin("space:create_index('primary', { type = 'hash' })")

if os.access(wal, os.F_OK) and not os.access(wal_inprogress, os.F_OK):
  print "00000000000000000002.xlog.inprogress has been successfully renamed"
server.stop()

print """
# Inprogress xlog must be renamed during regular termination.
"""
server.start()

wal_inprogress = os.path.join(server.vardir, "00000000000000000004.xlog.inprogress")
wal = os.path.join(server.vardir, "00000000000000000004.xlog")

admin("box.space[0]:insert{3, 'third tuple'}")

if os.access(wal_inprogress, os.F_OK):
  print "00000000000000000004.xlog.inprogress exists"

server.stop()

if os.access(wal, os.F_OK) and not os.access(wal_inprogress, os.F_OK):
  print "00000000000000000004.xlog.inprogress has been successfully renamed"

print """
# An inprogress xlog file with one record must be renamed during recovery.
"""

wal_inprogress = os.path.join(server.vardir, "00000000000000000005.xlog.inprogress")
wal = os.path.join(server.vardir, "00000000000000000005.xlog")

os.symlink(abspath("box/unfinished.xlog"), wal_inprogress)

server.start()

if os.access(wal, os.F_OK) and not os.access(wal_inprogress, os.F_OK):
  print "00000000000000000005.xlog.inprogress hash been successfully renamed"
server.stop()

print """
# Empty (zero size) inprogress xlog must be deleted during recovery.
"""

wal_inprogress = os.path.join(server.vardir, "00000000000000000006.xlog.inprogress")
wal = os.path.join(server.vardir, "00000000000000000006.xlog")

os.symlink(abspath("box/empty.xlog"), wal_inprogress)
server.start()

if not os.access(wal_inprogress, os.F_OK) and not os.access(wal, os.F_OK):
   print "00000000000000000006.xlog.inprogress has been successfully deleted"
server.stop()

print """
# Empty (header only, no records) inprogress xlog must be deleted
# during recovery.
"""

# If the previous test has failed, there is a dangling link
# and symlink fails.
try:
  os.symlink(abspath("box/just_header.xlog"), wal_inprogress)
except OSError as e:
  print e

server.start()

if not os.access(wal_inprogress, os.F_OK) and not os.access(wal, os.F_OK):
   print "00000000000000000006.xlog.inprogress has been successfully deleted"
server.stop()

print """
# Inprogress xlog with bad record must be deleted during recovery.
"""

# If the previous test has failed, there is a dangling link
# and symlink fails.
try:
  os.symlink(abspath("box/bad_record.xlog"), wal_inprogress)
except OSError as e:
  print e

server.start()

if not os.access(wal_inprogress, os.F_OK) and not os.access(wal, os.F_OK):
   print "00000000000000000006.xlog.inprogress has been successfully deleted"

print """
A test case for https://bugs.launchpad.net/tarantool/+bug/1052018
panic_on_wal_error doens't work for duplicate key errors
"""
server.stop()
server.cfgfile_source = "box/panic_on_wal_error.cfg"
server.deploy()
server.stop()
shutil.copy(abspath("box/dup_key1.xlog"),
            os.path.join(server.vardir, "00000000000000000002.xlog"))
shutil.copy(abspath("box/dup_key2.xlog"),
           os.path.join(server.vardir, "00000000000000000004.xlog"))
server.start()
admin("box.space[0]:get{1}")
admin("box.space[0]:get{2}")
admin("#box.space[0]")

# cleanup
server.stop()
server.deploy()
