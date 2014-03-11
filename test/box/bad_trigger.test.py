from lib.box_connection import BoxConnection
from tarantool import NetworkError
print """ 
 #
 # if on_connect() trigger raises an exception, the connection is dropped
 # 
 """

admin("function f1() nosuchfunction() end")
admin("box.session.on_connect(f1)")
try:
    con1 = BoxConnection('localhost', sql.port)
    con1("select * from t0 where k0=0")
    print "Connection is alive.\n"
except NetworkError as e:
    print "Connection is dead: {0}.\n".format(e.message)
# Clean-up
admin("box.session.on_connect(nil, f1)")
