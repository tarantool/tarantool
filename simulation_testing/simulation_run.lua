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


-- The main cycle
fiber.create(function()
    while true do
        local random_action = math.random(1, 11)

        if random_action < 5 then
            local rw_operation = math.random(1, 10)
            local operation
            if rw_operation < 3 then
                operation = randomized_operations.generate_random_read_operation(10)
            else
                operation = randomized_operations.generate_random_write_operation(10)
            end
            randomized_operations.execute_db_operation(my_functions.get_random_node(cg.replicas), "test", operation)
        elseif random_action < 9 then
            local type_of_crashing = math.random(1, 3)
            if type_of_crashing == 1 then
                local crash_node = crash_functions.get_random_nodes_for_crash(cg.replicas, nodes_activity_states, 1)
                if crash_node ~= -1 then
                    crash_functions.stop_node(
                        crash_node[1], 
                        5, 
                        10
                    )
                end

            elseif type_of_crashing == 2 then
                local crash_node = crash_functions.get_random_nodes_for_crash(cg.replicas, nodes_activity_states, 1)
                if crash_node ~= -1 then
                    crash_functions.create_delay_to_write_operations(
                        crash_node[1], 
                        "test", 
                        5, 
                        10
                    )
                end

            else 
                local crash_nodes = crash_functions.get_random_nodes_for_crash(cg.replicas, nodes_activity_states, 2)
                if crash_nodes ~= -1 then
                    crash_functions.break_connection_between_two_nodes(
                        crash_nodes, 
                        initial_replication, 
                        5, 
                        10
                    )
                end
            end    
        end

        fiber.sleep(math.random(1, 2)) 
    end
end)



print("[REPLICATION MONITOR] Started replication monitoring")

fiber.create(function(cg) replication_errors.run_replication_monitor(cg) end, cg)

print("[XLOG MONITOR] Started journals monitoring")

fiber.create(function() 
    while true do 
        log_handling.compare_two_random_xlogs("./replicas_dirs") 
        fiber.sleep(2)
    end
end)

