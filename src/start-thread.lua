remote = require('net.box')
cn = remote.connect(3301)

for i=1, 1100 ,1 do cn:execute("UPDATE sbtest1 SET c='lalala' WHERE id="..i.."") end
