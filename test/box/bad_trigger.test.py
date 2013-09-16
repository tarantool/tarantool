from lib.box_connection import BoxConnection
print """ 
 #
 # if on_connect() trigger raises an exception, the connection is dropped
 # 
 """

admin("type(box.session.on_connect(function() nosuchfunction() end))")
con1 = BoxConnection('localhost', server.primary_port)
con1("select * from t0 where k0=0")
if not con1.check_connection():
    print "Connection is dead.\n"
else:
    print "Connection is alive.\n"
# Clean-up
admin("type(box.session.on_connect(nil))")
