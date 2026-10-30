-- gh-4691: net.box fails to connect to tarantool-2.2+ server with
-- a schema of version 2.1.3 or below (w/o _vcollation system
-- space).

local t = require('luatest')
local server = require('luatest.server')

local g = t.group()

g.before_all(function()
    -- Start a server from a snapshot with the 2.1.3 schema.
    --
    -- The snapshot has a 'test' space with a 'unicode_ci' index
    -- part. We need it to verify that net.box will expose
    -- 'collation_id' for an index key part when collation names
    -- information is not available.
    --
    -- See test/box-luatest/gh_4691_data/fill.lua.
    g.schema_2_1_3 = server:new({
        alias = 'schema_2_1_3',
        datadir = 'test/box-luatest/gh_4691_data',
        box_cfg = {
            -- Prevent schema auto upgrading.
            read_only = true,
        },
    })

    -- Start the server, but don't try to connect to it. We'll do
    -- it as part of the test case, because if net.box is broken,
    -- it is unable to connect. It seems logical to fail the test
    -- case in this situation, not the before all hook.
    g.schema_2_1_3:start({wait_until_ready = false})
end)

g.after_all(function()
    g.schema_2_1_3:stop()
end)

g.test_connect_schema_2_1_3 = function()
    -- Create a net.box connection to the server. If _vcollation
    -- presence is required by the client, this call will fail
    -- after 60 seconds of attempts to connect.
    g.schema_2_1_3:wait_until_ready()
    local connection = g.schema_2_1_3.net_box

    -- Just is case: the connection is alive.
    t.assert(connection:is_connected())

    -- Just in case: the instance has an old schema version, which
    -- has no _vcollation view.
    local schema_version_tuple = connection.space._schema:get({'version'})
    t.assert_equals(schema_version_tuple, {'version', 2, 1, 3})

    -- Space metainfo is correct: 'collation_id' is exposed
    -- instead of 'collation' when collation name information is
    -- not available.
    local key_part = connection.space.test.index[0].parts[1]
    t.assert_is(key_part.collation, nil)
    t.assert_type(key_part.collation_id, 'number')
end
