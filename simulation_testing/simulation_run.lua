local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fiber = require('fiber')
local net_box = require('net.box')
local my_functions = require("my_functions")
local crash_functions = require("crash_functions")
local randomized_operations = require("randomized_operations")
local random_cluster = require('random_cluster')
local log_handling = require('log_handling')
local fio = require('fio')
local replication_errors = require("replication_errors")


io.output(assert(io.open("wroking_log.log", "w")))

function print(...)
    local t = {}
    for i = 1, select("#", ...) do
        t[i] = tostring(select(i, ...))
    end
    io.write(table.concat(t, "\t"), "\n")
end


math.randomseed(os.time())
random_cluster.clear_dirs_for_all_replicas()
local cg = random_cluster.rand_cluster(3)

box.cfg {
    checkpoint_count = 2, 
    memtx_use_mvcc_engine = true,
    memtx_dir = './memtx_dir',
    txn_isolation = 'best-effort' }

local initial_replication = my_functions.get_initial_replication(cg.replicas)

-- Checking the initial configuration
for _, node in ipairs(cg.replicas) do
    local node_state = node:exec(function()
        return box.info.election.state
    end)
    print(string.format("Node %s is %s", node.alias, tostring(node_state)))
    crash_functions.update_node_state(node, "active")
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

print("[[PERIODIC INSERT] Started")
log_handling.periodic_insert(
    cg,
    "test",
    1,
    1,
    0.01
)

print("[DIVERGENCE MONITOR] Started")
log_handling.divergence_monitor(
    cg,
    "test",
    100,
    1,
    2
)

print("[CRASH SIMULATION] Started")
local crash_time = 5 -- Crash-specific time, which sets the increased frequency of crashes
crash_functions.crash_simulation(
    cg,
    nodes_activity_states,
    initial_replication,
    1,
    crash_time,
    crash_time
)

--[[
fiber.create(function()
    while true do
        fiber.sleep(crash_time) 
        local type_of_crashing = math.random(1, 3)
        if type_of_crashing == 1 then
            local crash_node = crash_functions.get_random_nodes_for_crash(cg.replicas, nodes_activity_states, 1)
            if crash_node ~= -1 then
                crash_functions.stop_node(crash_node[1], 1, crash_time)
            end

        elseif type_of_crashing == 2 then
            local crash_node = crash_functions.get_random_nodes_for_crash(cg.replicas, nodes_activity_states, 1)
            if crash_node ~= -1 then
                crash_functions.create_delay_to_write_operations(crash_node[1], 1, crash_time)
            end

        else
            local crash_nodes = crash_functions.get_random_nodes_for_crash(cg.replicas, nodes_activity_states, 2)
            if crash_nodes ~= -1 then
                crash_functions.break_connection_between_two_nodes(crash_nodes, initial_replication, 1, crash_time)
            end
        end

    end
end)
]]--

print("[REPLICATION MONITOR] Started")
fiber.create(function(cg) replication_errors.run_replication_monitor(cg) end, cg)

