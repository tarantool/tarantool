from lib.box_connection import BoxConnection
from tarantool import NetworkError
print """ 
 #
 # if on_connect() trigger raises an exception, the connection is dropped
 # 
 """

server.admin("function f1() nosuchfunction() end")
server.admin("box.session.on_connect(f1)")
con1 = BoxConnection('localhost', server.sql.port)
con1("select * from t0 where k0=0")
# Clean-up
server.admin("box.session.on_connect(nil, f1)")
