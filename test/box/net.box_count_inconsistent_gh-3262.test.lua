remote = require 'net.box'
fiber = require 'fiber'
test_run = require('test_run').new()

LISTEN = require('uri').parse(box.cfg.listen)
space = box.schema.space.create('net_box_test_space')
index = space:create_index('primary', { type = 'tree' })

box.schema.user.grant('guest', 'read,write', 'space', 'net_box_test_space')
box.schema.user.grant('guest', 'execute', 'universe')

cn = remote.connect(box.cfg.listen)
cn.space[space.id]  ~= nil
cn.space.net_box_test_space ~= nil
cn.space.net_box_test_space ~= nil
cn.space.net_box_test_space.index ~= nil
cn.space.net_box_test_space.index.primary ~= nil
cn.space.net_box_test_space.index[space.index.primary.id] ~= nil
cn.space.net_box_test_space:insert{234, 1,2,3}
cn.space.net_box_test_space:replace{354, 1,2,4}
cn.space.net_box_test_space.index.primary:min(354)
cn.space.net_box_test_space.index.primary:max(234)
cn.space.net_box_test_space.index.primary:count(354)

box.schema.user.create('netbox', { password  = 'test' })
box.schema.user.grant('netbox', 'read,write', 'space', 'net_box_test_space')
box.schema.user.grant('netbox', 'execute', 'universe')
cn = remote.connect(LISTEN.host, LISTEN.service, { user = 'netbox', password = 'test' })
cn.state
cn.error
cn:ping()

function ret_after(to) fiber.sleep(to) return {{to}} end

cn:ping({timeout = 1.00})
cn:ping({timeout = 1e-9})
cn:ping()

remote_space = cn.space.net_box_test_space
remote_pk = remote_space.index.primary

remote_space:insert({0}, { timeout = 1.00 })
remote_space:insert({1}, { timeout = 1e-9 })
remote_space:insert({2})

remote_space:replace({0}, { timeout = 1e-9 })
remote_space:replace({1})
remote_space:replace({2}, { timeout = 1.00 })

remote_space:upsert({3}, {}, { timeout = 1e-9 })
remote_space:upsert({4}, {})
remote_space:upsert({5}, {}, { timeout = 1.00 })
remote_space:upsert({3}, {})

remote_space:update({3}, {}, { timeout = 1e-9 })
remote_space:update({4}, {})
remote_space:update({5}, {}, { timeout = 1.00 })
remote_space:update({3}, {})

remote_pk:update({5}, {}, { timeout = 1e-9 })
remote_pk:update({4}, {})
remote_pk:update({3}, {}, { timeout = 1.00 })
remote_pk:update({5}, {})

remote_space:get({0})
remote_space:get({1}, { timeout = 1.00 })
remote_space:get({2}, { timeout = 1e-9 })

remote_pk:get({3}, { timeout = 1e-9 })
remote_pk:get({4})
remote_pk:get({5}, { timeout = 1.00 })

remote_space:select({2}, { timeout = 1e-9 })
remote_space:select({2}, { timeout = 1.00 })
remote_space:select({2})

remote_pk:select({2}, { timeout = 1.00 })
remote_pk:select({2}, { timeout = 1e-9 })
remote_pk:select({2})

remote_space:select({5}, { timeout = 1.00, iterator = 'LE', limit = 5 })
remote_space:select({5}, { iterator = 'LE', limit = 5})
remote_space:select({5}, { timeout = 1e-9, iterator = 'LE', limit = 5 })

remote_pk:select({2}, { timeout = 1.00, iterator = 'LE', limit = 5 })
remote_pk:select({2}, { iterator = 'LE', limit = 5})
remote_pk:select({2}, { timeout = 1e-9, iterator = 'LE', limit = 5 })

remote_pk:count({2}, { timeout = 1.00})
remote_pk:count({2}, { timeout = 1e-9})
remote_pk:count({2})

remote_pk:count({2}, { timeout = 1.00, iterator = 'LE' })
remote_pk:count({2}, { iterator = 'LE'})
remote_pk:count({2}, { timeout = 1e-9, iterator = 'LE' })

remote_pk:min(nil, { timeout = 1.00 })
remote_pk:min(nil, { timeout = 1e-9 })
remote_pk:min(nil)

remote_pk:min({0}, { timeout = 1e-9 })
remote_pk:min({1})
remote_pk:min({2}, { timeout = 1.00 })

remote_pk:max(nil)
remote_pk:max(nil, { timeout = 1e-9 })
remote_pk:max(nil, { timeout = 1.00 })

remote_pk:max({0}, { timeout = 1.00 })
remote_pk:max({1}, { timeout = 1e-9 })
remote_pk:max({2})

--
-- gh-3262: index:count() inconsistent results
--
test_run:cmd("setopt delimiter ';'")

function do_count_test(min, it)
    local r1 = remote_pk:count(min, {iterator = it} )
    local r2 = box.space.net_box_test_space.index.primary:count(min, {iterator = it} )
    local r3 = remote.self.space.net_box_test_space.index.primary:count(min, {iterator = it} )
    return r1 == r2 and r2 == r3
end;

data = remote_pk:select();

for _, v in pairs(data) do
    local itrs = {'GE', 'GT', 'LE', 'LT' }
    for _, it in pairs(itrs) do
        assert(do_count_test(v[0], it) == true)
    end
end;

test_run:cmd("setopt delimiter ''");

_ = remote_space:delete({0}, { timeout = 1e-9 })
_ = remote_pk:delete({0}, { timeout = 1.00 })
_ = remote_space:delete({1}, { timeout = 1.00 })
_ = remote_pk:delete({1}, { timeout = 1e-9 })
_ = remote_space:delete({2}, { timeout = 1e-9 })
_ = remote_pk:delete({2})
_ = remote_pk:delete({3})
_ = remote_pk:delete({4})
_ = remote_pk:delete({5})

remote_space:get(0)
remote_space:get(1)
remote_space:get(2)

remote_space = nil

cn:call('ret_after', {0.01}, { timeout = 1.00 })
cn:call('ret_after', {1.00}, { timeout = 1e-9 })

cn:eval('return ret_after(...)', {0.01}, { timeout = 1.00 })
cn:eval('return ret_after(...)', {1.00}, { timeout = 1e-9 })

--
-- :timeout()
-- @deprecated since 1.7.4
--

cn:timeout(1).space.net_box_test_space.index.primary:select{234}
cn:call('ret_after', {.01})
cn:timeout(1):call('ret_after', {.01})
cn:timeout(.01):call('ret_after', {1})

cn = remote:timeout(0.0000000001):connect(LISTEN.host, LISTEN.service, { user = 'netbox', password = '123' })
cn:close()
cn = remote:timeout(1):connect(LISTEN.host, LISTEN.service, { user = 'netbox', password = '123' })

remote.self:ping()
remote.self.space.net_box_test_space:select{234}
remote.self:timeout(123).space.net_box_test_space:select{234}
remote.self:is_connected()
remote.self:wait_connected()

cn:close()
-- cleanup database after tests
space:drop()

box.schema.user.revoke('guest', 'execute', 'universe')
