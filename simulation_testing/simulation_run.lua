local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fiber = require('fiber')
local net_box = require('net.box')
local tools = require("tools")
local crash_functions = require("crash_functions")
local randomized_operations = require("randomized_operations")
local random_cluster = require('random_cluster')
local log_handling = require('log_handling')
local fio = require('fio')
local replication_errors = require("replication_errors")
local clock = require('clock')
logger = require('log')
os.remove('working_log.log')
logger.cfg { log = 'working_log.log' }


-- io.output(assert(io.open("working_log.log", "w")))


function log_info(...)
    -- Concatenate all arguments
    local t = {}
    for i = 1, select("#", ...) do
        t[i] = tostring(select(i, ...))
    end
    local msg = table.concat(t, "\t")

    -- log_info to stdout with a newline
    logger.info(string.format("%s %s\n", t, msg))
end

function log_error(...)
    -- Concatenate all arguments
    local t = {}
    for i = 1, select("#", ...) do
        t[i] = tostring(select(i, ...))
    end
    local msg = table.concat(t, "\t")

    -- log_info to stdout with a newline
    logger.error(string.format("%s %s\n", t, msg))
end


math.randomseed(os.time())
random_cluster.clear_dirs_for_all_replicas()
local cg = random_cluster.rand_cluster(4)
fiber.sleep(20)


box.cfg {
    checkpoint_count = 2,
    memtx_use_mvcc_engine = true,
    memtx_dir = './memtx_dir',
    txn_isolation = 'best-effort' }

local initial_replication = tools.get_initial_replication(cg.replicas)

-- Checking the initial configuration
for _, node in ipairs(cg.replicas) do
    local node_state = node:exec(function()
        return box.info.election.state
    end)
    log_info(string.format("Node %s is %s", node.alias, tostring(node_state)))
    crash_functions.update_node_state(node, "active")
end


-- Finding the leader node
local leader_node = tools.get_leader(cg.replicas)
if leader_node == nil then
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

log_info(result)

log_info("[[PERIODIC INSERT] Started")
log_handling.periodic_insert(
    cg,
    "test",
    1,
    1,
    0.1
)

log_info("[DIVERGENCE MONITOR] Started")
log_handling.divergence_monitor(
    cg,
    "test",
    100,
    1,
    2
)


log_info("[CRASH SIMULATION] Started")
local crash_time = 5 -- Crash-specific time, which sets the increased frequency of crashes
crash_functions.crash_simulation(
    cg,
    nodes_activity_states,
    initial_replication,
    1,
    crash_time,
    2 * crash_time
)

log_info("[REPLICATION MONITOR] Started")
fiber.create(function(cg) replication_errors.run_replication_monitor(cg) end, cg)

