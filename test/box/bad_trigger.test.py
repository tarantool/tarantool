from lib.box_connection import BoxConnection
print """ 
 #
 # if on_connect() trigger raises an exception, the connection is dropped
 # 
 """

admin("function f1() nosuchfunction() end")
admin("box.session.on_connect(f1)")
con1 = BoxConnection('localhost', sql.port)
con1("select * from t0 where k0=0")
if not con1.check_connection():
    print "Connection is dead.\n"
else:
    print "Connection is alive.\n"
# Clean-up
admin("box.session.on_connect(nil, f1)")
