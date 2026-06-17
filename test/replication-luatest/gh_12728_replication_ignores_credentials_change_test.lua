local t = require('luatest')
local cluster = require('luatest.replica_set')
local uri = require('uri')

local g = t.group()

local function update_uri_param(uri_str, val)
    local uri_parsed = uri.parse(uri_str)
    uri_parsed.params = { test = val }
    return uri.format(uri_parsed)
end

g.before_each(function()
    g.cluster = cluster:new({})
    g.master = g.cluster:build_server({alias = 'master'})
    g.master.net_box_uri = update_uri_param(g.master.net_box_uri, "before")
    local box_cfg = {
        replication = g.master.net_box_uri,
    }
    g.replica = g.cluster:build_server({alias = 'replica', box_cfg = box_cfg})

    g.cluster:add_server(g.master)
    g.cluster:add_server(g.replica)
    g.cluster:start()
    t.helpers.retrying({}, function()
        g.replica:assert_follows_upstream(1)
    end)
end)

g.after_each(function()
    g.cluster:drop()
end)

g.test_reconnect_uri_param_change = function(g)
    local new_master_uri = update_uri_param(g.master.net_box_uri, "after")
    g.master:update_box_cfg({listen = new_master_uri})
    g.replica:update_box_cfg({replication = new_master_uri})
    t.helpers.retrying({}, function()
        g.replica:assert_follows_upstream(1)
    end)
    g.replica:exec(function()
        local uri = require("uri")
        local param_val = uri.parse(
            box.info.replication[1].upstream.peer
        ).params.test
        t.assert_equals(param_val, {"after"})
    end)
end
