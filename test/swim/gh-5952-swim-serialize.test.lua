fiber = require('fiber')
swim = require('swim')
test_run = require('test_run').new()
test_run:cmd("push filter '\\.lua.*:[0-9]+: ' to '.lua:<line>: '")
test_run:cmd("push filter '127.0.0.1:[0-9]+$' to '127.0.0.1:<port>'")

--
-- gh-5952: invalid types in __serialize methods could lead to a crash.
--

s = swim.new({generation = 0})
getmetatable(s):__serialize()
getmetatable(s).__serialize(s)

s:cfg({uuid = uuid(1), uri = uri()})
getmetatable(s):__serialize()
getmetatable(s).__serialize(s)

self = s:self()
getmetatable(self):__serialize()
getmetatable(self).__serialize(self)

event = nil
_ = s:on_member_event(function(m, e) event = e end)
s:set_payload(1)
test_run:wait_cond(function() return event ~= nil end)

getmetatable(event):__serialize()
getmetatable(event).__serialize(event)

s:delete()

test_run:cmd("clear filter")
