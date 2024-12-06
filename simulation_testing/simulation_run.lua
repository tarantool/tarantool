local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fiber = require('fiber')
local net_box = require('net.box')
local my_functions = require("my_functions")
local crash_functions = require("crash_functions")
local randomized_operations = require("randomized_operations")
local replication_errors = require("replication_errors")

math.randomseed(os.time())

-- Starting and configuring a cluster
local cg = {}
cg.cluster = cluster:new()
cg.nodes = {}

local box_cfg = {
    election_mode = 'candidate',
    replication_sync_timeout = 1, 
    replication_timeout = 0.1,   
    replication = {
        server.build_listen_uri('node1', cg.cluster.id),
        server.build_listen_uri('node2', cg.cluster.id),
        server.build_listen_uri('node3', cg.cluster.id),
    },
}

cg.nodes[1] = cg.cluster:build_and_add_server({
    alias = 'node1',
    box_cfg = box_cfg,
})
cg.nodes[2] = cg.cluster:build_and_add_server({
    alias = 'node2',
    box_cfg = box_cfg,
})
cg.nodes[3] = cg.cluster:build_and_add_server({
    alias = 'node3',
    box_cfg = box_cfg,
})

fiber.sleep(1) 

cg.cluster:start()

local initial_replication = my_functions.get_initial_replication(cg.nodes)

-- Checking the initial configuration
for _, node in ipairs(cg.nodes) do
    local node_state = node:exec(function()
        return box.info.election.state
    end)
    print(string.format("Node %s is %s", node.alias, tostring(node_state)))
end

-- Finding the leader node
local leader_node = cg.cluster:get_leader()
if not leader_node then
    error("The leader has not been found. Make sure that replication and elections are configured!!!")
end

--Creating a synchro test space
local result = leader_node:exec(function()
    local message = ""

    if not box.cfg then
        error("box.cfg{} was not called!")
    end

    if not box.space.test then
        local space = box.schema.create_space('test', {
            format = {
                {name = 'id', type = 'unsigned'},
                {name = 'value', type = 'string'},
            },
            is_sync = true
        })
        space:create_index('primary', {parts = {'id'}})
        message = "The 'test' space is created and configured on the leader"
    else
        box.space.test:truncate()
        message = "The 'test' space has already been previously created and configured on the callout"
    end
    return message
end)

print(result)


-- The main cycle
fiber.create(function()
    while true do
        local random_action = math.random(1, 10)

        if random_action < 8 then
            randomized_operations.do_random_operation(my_functions.get_random_node(cg.nodes), "test", 10)
        else 
            local type_of_crashing = math.random(1, 3)
            if type_of_crashing == 1 then
                crash_functions.stop_node(my_functions.get_random_node(cg.nodes), 5, 10)

            elseif type_of_crashing == 2 then
                crash_functions.create_delay_to_write_operations(my_functions.get_random_node(cg.nodes), "test", 5, 10)

            else 
                crash_functions.break_connection_between_random_nodes(cg.nodes, initial_replication, 5, 10)
    
            end
        end

        fiber.sleep(math.random(1, 2)) 
    end
end)


fiber.create(function()
    box.cfg{
            memtx_use_mvcc_engine = true,
            memtx_dir = './memtx_dir',
            wal_dir = './wal_dir',
            hot_standby = true
    }

    print("[Replication Monitor] Started monitoring")

    fiber.create(function(cg) replication_errors.run_replication_monitor(cg) end, cg)
    
end)