package.path = package.path .. "../src/?.lua"
local fiber = require('fiber')
local fio = require('fio')


local crash_functions = require("crash_functions")
local logger = require("logger")
local fio_utils = require("fio_utils")
local log_handling = require('log_handling')
local random_cluster = require('random_cluster')
local replication_errors = require("replication_errors")
local proxy_handling = require('proxy_handling')
local tools = require("tools")


math.randomseed(os.time())

_G.SUCCESSFUL_LOGS = os.getenv("ENV_SUCCESSFUL_LOGS") or "false"
_G.WITHOUT_BEST_EFFORT = os.getenv("WITHOUT_BEST_EFFORT") or "false"
_G.WITHOUT_LINEARIZABLE = os.getenv("WITHOUT_LINEARIZABLE") or "false"
_G.WITHOUT_PROXY = os.getenv("WITHOUT_PROXY") or "false"
_G.WORKING_LOG_PATH = os.getenv("WORKING_LOG_PATH") or "./working_log.log"
print("ENV_SUCCESSFUL_LOGS: " .. _G.SUCCESSFUL_LOGS)
print("WITHOUT_BEST_EFFORT: " .. _G.WITHOUT_BEST_EFFORT)
print("WITHOUT_LINEARIZABLE: " .. _G.WITHOUT_LINEARIZABLE)
print("WITHOUT_PROXY: " .. _G.WITHOUT_PROXY)
print("WORKING_LOG_PATH: " .. _G.WORKING_LOG_PATH)
Logger = logger.Logger
logger.init_logger()

local cg = random_cluster.rand_cluster(5)

local initial_replication = tools.get_initial_replication(cg.replicas)

-- Checking the initial configuration
for _, node in ipairs(cg.replicas) do
    local node_state = node:exec(function()
        return box.info.election.state
    end)
    LogInfo(string.format("Node %s is %s", node.alias, tostring(node_state)))
    crash_functions.update_node_state(node, "active")
end

-- Checking the initial configuration for proxies
for _, proxy in ipairs(cg.proxies) do
    local proxy_state = proxy_handling.get_proxy_state(proxy) 
    LogInfo(string.format("Proxy %s is %s", proxy.alias, tostring(proxy_state)))
    crash_functions.update_node_state(proxy, "active")
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

LogInfo(result)

LogInfo("[PERIODIC INSERT] Started")
log_handling.periodic_insert(
    cg,
    "test",
    1,
    1,
    0.1
)

LogInfo("[DIVERGENCE MONITOR] Started")
log_handling.divergence_monitor(
    cg,
    "test",
    100,
    1,
    2
)


LogInfo("[CRASH SIMULATION] Started")
local crash_time = 60 -- Crash-specific time, which sets the increased frequency of crashes
crash_functions.random_crash_simulation(
    cg,
    _G.nodes_activity_states,
    initial_replication,
    1,
    10,
    11
)

LogInfo("[REPLICATION MONITOR] Started")
fiber.create(function(cg) replication_errors.run_replication_monitor(cg) end, cg)

