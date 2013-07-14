# encoding: utf-8
from lib.admin_connection import AdminConnection
from lib.box_connection import BoxConnection

admin("box.session.exists(box.session.id())")
admin("box.session.exists()")
admin("box.session.exists(1, 2, 3)")
admin("box.session.exists(1234567890)")

# check session.id()
admin("box.session.id() > 0")
admin("f = box.fiber.create(function() box.fiber.detach() failed = box.session.id() ~= 0 end)")
admin("box.fiber.resume(f)")
admin("failed")
admin("f1 = box.fiber.create(function() if box.session.id() == 0 then failed = true end end)")
admin("box.fiber.resume(f1)")
admin("failed")
admin("box.session.peer() == box.session.peer(box.session.id())")

# check on_connect/on_disconnect triggers
admin("box.session.on_connect(function() end)")
admin("box.session.on_disconnect(function() end)")

# check it's possible to reset these triggers
#
admin("type(box.session.on_connect(function() error('hear') end))")
admin("type(box.session.on_disconnect(function() error('hear') end))")

# check on_connect/on_disconnect argument count and type
admin("box.session.on_connect()")
admin("box.session.on_disconnect()")

admin("box.session.on_connect(function() end, function() end)")
admin("box.session.on_disconnect(function() end, function() end)")

admin("box.session.on_connect(1, 2)")
admin("box.session.on_disconnect(1, 2)")

admin("box.session.on_connect(1)")
admin("box.session.on_disconnect(1)")

# use of nil to clear the trigger
admin("type(box.session.on_connect(nil))")
admin("type(box.session.on_disconnect(nil))")
admin("type(box.session.on_connect(nil))")
admin("type(box.session.on_disconnect(nil))")

# check how connect/disconnect triggers work
admin("function inc() active_connections = active_connections + 1 end")
admin("function dec() active_connections = active_connections - 1 end")
admin("box.session.on_connect(inc)")
admin("box.session.on_disconnect(dec)")
admin("active_connections = 0")
con1 = AdminConnection('localhost', server.admin_port)
con1("active_connections")
con2 = AdminConnection('localhost', server.admin_port)
con2("active_connections")
con1.disconnect()
con2.disconnect()
admin("type(box.session.on_connect(nil))")
admin("type(box.session.on_disconnect(nil))")

# write audit trail of connect/disconnect into a space
admin("box.session.on_connect(function() box.insert(0, box.session.id()) end)")
admin("box.session.on_disconnect(function() box.delete(0, box.session.id()) end)")
con1("box.unpack('i', box.select(0, 0, box.session.id())[0]) == box.session.id()")
con1.disconnect()

# if on_connect() trigger raises an exception, the connection is dropped
admin("type(box.session.on_connect(function() nosuchfunction() end))")
con1 = BoxConnection('localhost', server.primary_port)
try:
    con1.execute("select * from t0 where k0=0")
    con1.execute("select * from t0 where k0=0")
except Exception as e:
    print "disconnected"

# cleanup

admin("type(box.session.on_connect(nil))")
admin("type(box.session.on_disconnect(nil))")
admin("active_connections")

# vim: syntax=python
