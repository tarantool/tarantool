package.cpath  = "?.so"

os.execute("mkdir -p box")
os.execute("cp ../../src/module/sophia/sophia.so box/")

sophia = require("box.sophia")

env = sophia.create()
flags = bit.bor(sophia.SPO_RDWR, sophia.SPO_CREAT)
env:ctl(sophia.SPDIR, flags, "./sophia")
env:open()
t = {}
for key=1, 10 do table.insert(t, env:set(tostring(key), tostring(key))) end
t
t = {}
for key=1, 10 do table.insert(t, env:get(tostring(key))) end
t
t = {}
env:close()

env = sophia.create()
flags = bit.bor(sophia.SPO_RDWR)
env:ctl(sophia.SPDIR, flags, "./sophia")
env:open()
t = {}
for key=1, 10 do table.insert(t, env:get(tostring(key))) end
t
t = {}
for key=1, 10 do table.insert(t, env:delete(tostring(key))) end
t
t = {}
for key=1, 10 do table.insert(t, env:get(tostring(key))) end
t
t = {}
env:close()

os.execute("rm -rf box/")
