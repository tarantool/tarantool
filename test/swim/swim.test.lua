fiber = require('fiber')
swim = require('swim')
test_run = require('test_run').new()
test_run:cmd("push filter '\\.lua.*:[0-9]+: ' to '.lua:<line>: '")
test_run:cmd("push filter '127.0.0.1:[0-9]+$' to '127.0.0.1:<port>'")
msgpack = require('msgpack')
ffi = require('ffi')
--
-- gh-3234: SWIM gossip protocol.
--

-- Invalid cfg parameters.
swim.new(1)
swim.new({uri = true})
swim.new({heartbeat_rate = 'rate'})
swim.new({ack_timeout = 'timeout'})
swim.new({gc_mode = 'not a mode'})
swim.new({gc_mode = 0})
swim.new({uuid = 123})
swim.new({uuid = '1234'})

-- Valid parameters, but invalid configuration.
swim.new({})
swim.new({uuid = uuid(1)})
swim.new({uri = uri()})

-- Check manual deletion.
s = swim.new({uuid = uuid(1), uri = uri()})
s:delete()
s:cfg({})
s = nil
_ = collectgarbage('collect')

s = swim.new({uuid = uuid(1), uri = uri()})
s:quit()
s:is_configured()
s = nil
_ = collectgarbage('collect')

s = swim.new({uuid = uuid(1), uri = uri()})
s:is_configured()
s:size()
s.cfg
s.cfg.gc_mode = 'off'
s:cfg{gc_mode = 'off'}
s.cfg
s.cfg.gc_mode
s.cfg.uuid
s.cfg()
s:cfg({wrong_opt = 100})
s:delete()

-- Reconfigure.
s = swim.new()
s:is_configured()
-- Check that not configured instance does not provide most of
-- methods.
s.quit == nil
s:cfg({uuid = uuid(1), uri = uri()})
s.quit ~= nil
s:is_configured()
s:size()
s = nil
_ = collectgarbage('collect')

-- Invalid usage.
s = swim.new()
s.delete()
s.is_configured()
s.cfg()
s:delete()

--
-- Basic member table manipulations.
--
s1 = swim.new({uuid = uuid(1), uri = uri(), heartbeat_rate = 0.01, generation = 0})
s2 = swim.new({uuid = uuid(2), uri = listen_uri, heartbeat_rate = 0.01, generation = 0})

s1.broadcast()
s1:broadcast('wrong port')
-- Note, broadcast takes a port, not a URI.
s1:broadcast('127.0.0.1:3333')
-- Ok to broadcast on default port.
s1:broadcast()

s1:broadcast(listen_port)
while s2:size() ~= 2 or s1:size() ~= 2 do fiber.sleep(0.01) s1:broadcast(listen_port) end

s2:delete()

s1.remove_member()
s1:remove_member(100)
s1:remove_member('1234')
s1:remove_member(uuid(2)) size = s1:size()
size

s1.add_member()
s1:add_member(100)
s1:add_member({uri = true})
s1:add_member({uri = listen_uri})
s1:add_member({uuid = uuid(2)})
s1:remove_member(uuid(2)) s1:add_member({uri = listen_uri, uuid = uuid(2)})
s1:add_member({uri = listen_uri, uuid = uuid(2)})
s1:size()

s1:cfg({uuid = uuid(3)}) old_self = s1:member_by_uuid(uuid(1))
s1:self():uuid()
old_self
-- Can't remove self.
s1:remove_member(uuid(3))
-- Not existing.
s1:remove_member(uuid(4))
-- Old self.
s1:remove_member(uuid(1))

s1:delete()

s1 = swim.new({uuid = uuid(1), uri = uri()})
s2 = swim.new({uuid = uuid(2), uri = uri()})
s1.probe_member()
s1:probe_member()
s1:probe_member(true)
-- Not existing URI is ok - nothing happens.
s1:probe_member('127.0.0.1:1')
fiber.yield()
s1:size()
s1:probe_member(s2:self():uri())
while s1:size() ~= 2 do fiber.sleep(0.01) end
s2:size()

s1:delete()
s2:delete()

--
-- Member API.
--
s1 = swim.new({uuid = uuid(1), uri = uri(), generation = 0})
s = s1:self()
s
s:status()
s:uuid()
s:uuid() == s:uuid()
s:uri()
s:incarnation()
s:payload_cdata()
s:payload_str()
s:payload()
s:is_dropped()
s.unknown_index

s.status()
s.uuid()
s.uri()
s.incarnation()
s.payload_cdata()
s.payload_str()
s.payload()
s.is_dropped()

s1:member_by_uuid(uuid(1)) ~= nil
s1:member_by_uuid(50)
-- gh-5951: could crash with no arguments or NULL.
s1:member_by_uuid()
s1:member_by_uuid(box.NULL)
s1:member_by_uuid(uuid(2))

-- UUID can be cdata.
s1:member_by_uuid(s:uuid())

s1:quit()
s:status()
s:is_dropped()

--
-- Payload.
--
s = swim.new({uuid = uuid(1), uri = uri()})
s.set_payload()
s.set_payload_raw()

self = s:self()
s:set_payload()
self:payload()
s:set_payload({a = 100})
self:payload()
s:set_payload(100)
self:payload()
s:set_payload(false)
self:payload()

p = self:payload_str()
p
(msgpack.decode(p))

p, size = self:payload_cdata()
type(p)
size
(msgpack.decode(p, size))

s:set_payload(string.rep('a', 1500))
self:payload()
s:set_payload()
self:payload()

s:set_payload(100)
self:payload()
s:set_payload_raw()
self:payload()

-- Raw payload setting can be used when MessagePack is not needed,
-- or already encoded.
s:set_payload_raw(nil, '123')
s:set_payload_raw('123', -1)

size = 10
cdata = ffi.new('int[?]', size)
for i = 0, size - 1 do cdata[i] = i end
bsize = ffi.sizeof('int') * size
s:set_payload_raw(cdata)
s:set_payload_raw('str', 4)
s:set_payload_raw(true)

s:set_payload_raw(cdata, bsize)
self:payload_str():len() == bsize
self_cdata, self_bsize = self:payload_cdata()
self_bsize == bsize
self_cdata = ffi.cast('int *', self_cdata)
for i = 0, size - 1 do assert(self_cdata[i] == cdata[i]) end

s:set_payload_raw('raw str')
self:payload_str()
s:set_payload_raw('raw str', 3)
self:payload_str()

s:delete()
self:is_dropped()

--
-- Check payload dissemination.
--
s1 = swim.new({uuid = uuid(1), uri = uri(), heartbeat_rate = 0.01, generation = 0})
s2 = swim.new({uuid = uuid(2), uri = uri(), heartbeat_rate = 0.01, generation = 0})
s1:add_member({uuid = uuid(2), uri = s2:self():uri()})
while s2:size() ~= 2 do fiber.sleep(0.01) end
s1_view = s2:member_by_uuid(uuid(1))
s1_view:payload()
s1_view:incarnation()

s1:set_payload('payload')
while s1_view:payload() ~= 'payload' do fiber.sleep(0.01) end
s1_view:incarnation()

s1:set_payload('payload2')
while s1_view:payload() ~= 'payload2' do fiber.sleep(0.01) end
s1_view:incarnation()

s1:delete()
s2:delete()

--
-- Iterators.
--
function iterate() local t = {} for k, v in s:pairs() do table.insert(t, {k, v}) end return t end
s = swim.new({generation = 0})
iterate()
s:cfg({uuid = uuid(1), uri = uri(), gc_mode = 'off'})
s.pairs()
iterate()
s:add_member({uuid = uuid(2), uri = uri()})
iterate()
s:add_member({uuid = uuid(3), uri = uri()})
iterate()
s:delete()

--
-- Payload caching.
--
s1 = swim.new({uuid = uuid(1), uri = uri(), heartbeat_rate = 0.01, generation = 0})
s2 = swim.new({uuid = uuid(2), uri = uri(), heartbeat_rate = 0.01, generation = 0})
s1_self = s1:self()
_ = s1:add_member({uuid = s2:self():uuid(), uri = s2:self():uri()})
_ = s2:add_member({uuid = s1_self:uuid(), uri = s1_self:uri()})
s1:size()
s2:size()
s1_view = s2:member_by_uuid(s1_self:uuid())

s1:set_payload({a = 100})
p = s1_self:payload()
s1_self:payload() == p

while not s1_view:payload() do fiber.sleep(0.01) end
p = s1_view:payload()
s1_view:payload() == p

s1:delete()
s2:delete()

--
-- Member table cache in Lua.
--
s = swim.new({uuid = uuid(1), uri = uri()})
self = s:self()
s:self() == self

s:add_member({uuid = uuid(2), uri = 1})
s2 = s:member_by_uuid(uuid(2))
s2
-- Next lookups return the same member table.
s2_old_uri = s2:uri()

-- Check, that it is impossible to take removed member from the
-- cached table.
s:remove_member(uuid(2))
s:member_by_uuid(uuid(2))

-- GC automatically removes members from the member table.
self = nil
s2 = nil
collectgarbage('collect')
s.cache_table
s:add_member({uuid = uuid(2), uri = 2})
s2 = s:member_by_uuid(uuid(2))
s2:uri() ~= s2_old_uri

s:delete()

--
-- Encryption.
--
s1 = swim.new({uuid = uuid(1), uri = uri(), generation = 0})
s2 = swim.new({uuid = uuid(2), uri = uri(), generation = 0})

s1.set_codec()
s1:set_codec(100)
s1:set_codec({})
s1:set_codec({algo = 100})
cfg = {algo = 'aes128', mode = 'unknown'}
s1:set_codec(cfg)
cfg.mode = 'cbc'
s1:set_codec(cfg)
cfg.key_size = 'str'
s1:set_codec(cfg)
cfg.key_size = 200
cfg.key = true
s1:set_codec(cfg)
cfg.key = 'key'
s1:set_codec(cfg)
cfg.key = ffi.new('char[?]', 20)
cfg.key_size = nil
s1:set_codec(cfg)
cfg.key_size = 20
s1:set_codec(cfg)

cfg.key = '1234567812345678'
cfg.key_size = 16
s1:set_codec(cfg)

-- S2 uses different encryption and can't decode the ping.
s1:probe_member(s2:self():uri())
fiber.sleep(0.01)
s1:size()
s2:size()

s2:set_codec(cfg)
s1:probe_member(s2:self():uri())
while s1:size() ~= 2 do fiber.sleep(0.01) end
s2:size()

s1:remove_member(s2:self():uuid()) s2:remove_member(s1:self():uuid())

-- Set different private key - again can't decode.
cfg.key = '8765432187654321'
cfg.key_size = nil
s2:set_codec(cfg)

s1:probe_member(s2:self():uri())
fiber.sleep(0.01)
s1:size()
s2:size()

cfg.key = '12345678'
cfg.algo = 'des'
cfg.mode = 'cfb'
s1:set_codec(cfg)

s1:probe_member(s2:self():uri())
fiber.sleep(0.01)
s1:size()
s2:size()

s1:set_codec({algo = 'none'})
s2:set_codec({algo = 'none'})
s1:probe_member(s2:self():uri())
while s1:member_by_uuid(s2:self():uuid()) == nil do fiber.sleep(0.01) end
s2:member_by_uuid(s1:self():uuid())

s1:delete()
s2:delete()

--
-- gh-4250: allow to set triggers on a new member appearance, old
-- member drop, member update.
--
s1 = swim.new({generation = 0})
s1.on_member_event()

m_list = {}
e_list = {}
ctx_list = {}
f = nil
f_need_sleep = false
_ = test_run:cmd("setopt delimiter ';'")
t_save_event = function(m, e, ctx)
    table.insert(m_list, m)
    table.insert(e_list, e)
    table.insert(ctx_list, ctx)
end;
t_yield = function(m, e, ctx)
    f = fiber.self()
    t_save_event(m, e, ctx)
    while f_need_sleep do fiber.sleep(10000) end
end;
_ = test_run:cmd("setopt delimiter ''");
t_save_event_id = s1:on_member_event(t_save_event, 'ctx')
-- Not equal, because SWIM wraps user triggers with a closure for
-- context preprocessing.
t_save_event_id ~= t_save_event

s1:cfg{uuid = uuid(1), uri = uri(), heartbeat_rate = 0.01}
while #m_list < 1 do fiber.sleep(0) end
m_list
e_list
ctx_list
m_list = {} e_list = {} ctx_list = {}

t_yield_id = s1:on_member_event(t_yield, 'ctx2')
f_need_sleep = true
s2 = swim.new({uuid = uuid(2), uri = uri(), heartbeat_rate = 0.01, generation = 0})
s2:add_member({uuid = s1:self():uuid(), uri = s1:self():uri()})
while s1:size() ~= 2 do fiber.sleep(0.01) end
-- Only first trigger worked. Second is waiting, because first
-- sleeps.
m_list
e_list
ctx_list
m_list = {} e_list = {} ctx_list = {}
-- But it does not prevent normal SWIM operation.
s1:set_payload('payload')
while not s2:member_by_uuid(s1:self():uuid()):payload() do fiber.sleep(0.01) end
s2:member_by_uuid(s1:self():uuid()):payload()

f_need_sleep = false
fiber.wakeup(f)
while #m_list ~= 3 do fiber.sleep(0.01) end
m_list
e_list
ctx_list
m_list = {} e_list = {} ctx_list = {}
#s1:on_member_event()

s1:on_member_event(nil, t_yield_id)
s2:quit()
while s1:size() ~= 1 do fiber.sleep(0.01) end
-- Process event.
fiber.sleep(0)
-- Two events - status update to 'left', and 'drop'.
m_list
e_list
ctx_list
m = m_list[1]
-- Cached member table works even when a member is deleted.
m_list[1] == m_list[2]
m_list = {} e_list = {} ctx_list = {}

s1:on_member_event(nil, t_save_event_id)
s1:add_member({uuid = m:uuid(), uri = m:uri()})
fiber.sleep(0)
-- No events - all the triggers are dropped.
m_list
e_list
ctx_list

s1:delete()

--
-- gh-4280: 'generation' counter to detect restarts and refute
-- information left from previous lifes of a SWIM instance.
--
s1 = swim.new({uuid = uuid(1), uri = 0, heartbeat_rate = 0.01, generation = 0})
s2 = swim.new({uuid = uuid(2), uri = 0, heartbeat_rate = 0.01})
s2:add_member({uuid = uuid(1), uri = s1:self():uri()})
s1_view = s2:member_by_uuid(uuid(1))
s1:set_payload('payload 1')
while not s1_view:payload() do fiber.sleep(0.1) end
s1_view:payload()
s1:self():incarnation()

-- Now S2 knows S1's payload as 'payload 1'.

new_gen_ev = nil
_ = s2:on_member_event(function(m, e) if e:is_new_generation() then new_gen_ev = e end end)
s1:delete()
s1 = swim.new({uuid = uuid(1), uri = 0, heartbeat_rate = 0.01, generation = 1})
s1:add_member({uuid = uuid(2), uri = s2:self():uri()})
s1:set_payload('payload 2')
-- Without the new generation S2 would believe that S1's payload
-- is still 'payload 1'. Because 'payload 1' and 'payload 2' were
-- disseminated with the same version.
s1:self():incarnation()

while s1_view:payload() ~= 'payload 2' do fiber.sleep(0.1) end
s1_view:payload()
new_gen_ev

-- Generation is static parameter.
s1:cfg({generation = 5})

s1:delete()
s2:delete()

-- It is allowed to create a SWIM with a generation value, but do
-- not configure it.
swim.new({generation = 0})

--
-- Check that Lua triggers don't keep a reference of SWIM instance
-- preventing its GC.
--
s = swim.new({uri = 0, uuid = uuid(1)})
_ = s:on_member_event(function() end)
s = setmetatable({s}, {__mode = 'v'})
collectgarbage('collect')
s

--
-- Check that SWIM GC doesn't block nor crash garbage collector.
--
s = swim.new()
allow_gc = false
_ = s:on_member_event(function() while not allow_gc do pcall(fiber.sleep, 0.01) end end)
s:cfg({uri = 0, uuid = uuid(1)})
s = setmetatable({s}, {__mode = 'v'})
collectgarbage('collect')
s
allow_gc = true

test_run:cmd("clear filter")
