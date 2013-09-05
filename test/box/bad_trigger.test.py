from lib.box_connection import BoxConnection
print """ 
 #
 # if on_connect() trigger raises an exception, the connection is dropped
 # 
 """

admin("type(box.session.on_connect(function() nosuchfunction() end))")
con1 = BoxConnection('localhost', server.primary_port)
try:
    con1.execute("select * from t0 where k0=0")
    con1.execute("select * from t0 where k0=0")
except Exception as e:
    print "Exception raised\n"

# Clean-up
admin("type(box.session.on_connect(nil))")
