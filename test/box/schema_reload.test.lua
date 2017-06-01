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
old_schema_id = cn._schema_id

-- create one more space
s2 = box.schema.create_space('test2')
i2 = s2:create_index('primary')

----------------------------------
-- TEST #1 simple reload
----------------------------------
-- check that schema is not fresh
cn.space.test2 == nil
cn._schema_id == old_schema_id

-- exec request with reload
cn.space.test:select{}
cn._schema_id > old_schema_id

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
-- gh-1808: support schema_id in CALL, EVAL and PING
--------------------------------------------------------------------------------

test_run:cmd('setopt delimiter ";"')
function bump_schema_id()
    if box.space.bump_schema_id == nil then
        box.schema.create_space('bump_schema_id')
    else
        box.space.bump_schema_id:drop()
    end
end;
test_run:cmd('setopt delimiter ""');

cn = net_box.connect(box.cfg.listen)

-- ping
schema_id = cn._schema_id
bump_schema_id()
cn:ping()
-- Sic: net.box returns true on :ping() even on ER_WRONG_SCHEMA_VERSION
while cn._schema_id == schema_id do fiber.sleep(0.0001) end
cn._schema_id == schema_id + 1

-- call
schema_id = cn._schema_id
bump_schema_id()
function somefunc() return true end
cn:call('somefunc')
cn._schema_id == schema_id + 1
somefunc = nil

-- failed call
schema_id = cn._schema_id
bump_schema_id()
cn:call('somefunc')
cn._schema_id == schema_id + 1

-- eval
schema_id = cn._schema_id
bump_schema_id()
cn:eval('return')
cn._schema_id == schema_id + 1
somefunc = nil

-- failed eval
schema_id = cn._schema_id
bump_schema_id()
cn:eval('error("xx")')
cn._schema_id == schema_id + 1
somefunc = nil

cn:close()

if box.space.bump_schema_id ~= nil then box.space.bump_schema_id:drop() end

box.schema.user.revoke('guest', 'read,write,execute', 'universe')
