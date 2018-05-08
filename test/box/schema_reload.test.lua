box.schema.user.grant('guest', 'read,write,execute', 'universe')
net_box = require('net.box')
fiber = require('fiber')
LISTEN = require('uri').parse(box.cfg.listen)

-- create first space
s = box.schema.create_space('test')
i = s:create_index('primary')
cn = net_box.connect(LISTEN.host, LISTEN.service)

-- check that schema is correct
cn.space.test ~= nil
old_schema_version = cn.schema_version

-- create one more space
s2 = box.schema.create_space('test2')
i2 = s2:create_index('primary')

----------------------------------
-- TEST #1 simple reload
----------------------------------
-- check that schema is not fresh
cn.space.test2 == nil
cn.schema_version == old_schema_version

-- exec request with reload
cn.space.test:select{}
cn.schema_version > old_schema_version

----------------------------------
-- TEST #2 parallel select/reload
----------------------------------
env = require('test_run')
test_run = env.new()

requests = 0
reloads = 0

test_run:cmd('setopt delimiter ";"')
function selector()
    while true do
        cn.space.test:select{}
        requests = requests + 1
        fiber.sleep(0.01)
    end
end

function reloader()
    while true do
        cn:reload_schema()
        reloads = reloads + 1
        fiber.sleep(0.001)
    end
end;
test_run:cmd('setopt delimiter ""');

request_fiber = fiber.create(selector)
reload_fiber = fiber.create(reloader)

-- Check that each fiber works
while requests < 10 or reloads < 10 do fiber.sleep(0.01) end
requests < reloads

-- cleanup
request_fiber:cancel()
reload_fiber:cancel()
s:drop()
s2:drop()

--------------------------------------------------------------------------------
-- gh-1808: support schema_version in CALL, EVAL and PING
--------------------------------------------------------------------------------

test_run:cmd('setopt delimiter ";"')
function bump_schema_version()
    if box.space.bump_schema_version == nil then
        box.schema.create_space('bump_schema_version')
    else
        box.space.bump_schema_version:drop()
    end
end;
test_run:cmd('setopt delimiter ""');

cn = net_box.connect(box.cfg.listen)

-- ping
schema_version = cn.schema_version
bump_schema_version()
cn:ping()
function wait_new_schema() while cn.schema_version == schema_version do fiber.sleep(0.0001) end end
wait_new_schema()
cn.schema_version == schema_version + 1

-- call
schema_version = cn.schema_version
bump_schema_version()
function somefunc() return true end
cn:call('somefunc')
wait_new_schema()
cn.schema_version == schema_version + 1
somefunc = nil

-- failed call
schema_version = cn.schema_version
bump_schema_version()
cn:call('somefunc')
wait_new_schema()
cn.schema_version == schema_version + 1

-- eval
schema_version = cn.schema_version
bump_schema_version()
cn:eval('return')
wait_new_schema()
cn.schema_version == schema_version + 1
somefunc = nil

-- failed eval
schema_version = cn.schema_version
bump_schema_version()
cn:eval('error("xx")')
wait_new_schema()
cn.schema_version == schema_version + 1
somefunc = nil

cn:close()

-- box.internal.schema_version()
schema_version = box.internal.schema_version()
schema_version > 0
bump_schema_version()
box.internal.schema_version() == schema_version + 1

if box.space.bump_schema_version ~= nil then box.space.bump_schema_version:drop() end

box.schema.user.revoke('guest', 'read,write,execute', 'universe')
