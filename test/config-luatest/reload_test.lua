local t = require('luatest')
local treegen = require('luatest.treegen')
local server = require('luatest.server')

local g = t.group()

g.after_each(function()
    if g.server ~= nil then
        g.server:stop()
    end
end)

g.test_no_cluster_config_failure = function(g)
    local dir = treegen.prepare_directory({}, {})
    local config = [[
        credentials:
          users:
            guest:
              roles:
              - super
        iproto:
          listen:
            - uri: unix/:./{{ instance_name }}.iproto
        groups:
          group-001:
            replicasets:
              replicaset-001:
                instances:
                  instance-001: {}
    ]]
    local config_file = treegen.write_file(dir, 'config.yaml', config)
    local opts = {
        config_file = config_file,
        alias = 'instance-001',
        chdir = dir,
    }
    g.server = server:new(opts)
    g.server:start()

    --
    -- Make sure that the error starts with "Reload" if the configuration did
    -- not contain a cluster configuration at the time of the reload.
    --
    config = [[{}]]
    treegen.write_file(dir, 'config.yaml', config)
    g.server:exec(function()
        local config = require('config')
        local ok, err = pcall(config.reload, config)
        t.assert(not ok)
        t.assert(string.startswith(err, 'Reload failure.\n\nNo cluster config'))
    end)

    --
    -- Make sure that the error starts with "Reload" if the instance was not
    -- found in the cluster configuration at the time of the reload.
    --
    config = [[
        credentials:
          users:
            guest:
              roles:
              - super
        iproto:
          listen:
            - uri: unix/:./{{ instance_name }}.iproto
    ]]
    treegen.write_file(dir, 'config.yaml', config)
    g.server:exec(function()
        local config = require('config')
        local ok, err = pcall(config.reload, config)
        t.assert(not ok)
        t.assert(string.startswith(err, 'Reload failure.\n\nUnable to find'))
    end)
end
