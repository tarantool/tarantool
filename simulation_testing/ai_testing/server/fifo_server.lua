package.path = package.path .. ";../../src/?.lua"

local json = require("json")
local cluster = require('random_cluster')
local logger = require("logger")
local crash_functions = require("crash_functions")
local tools = require("tools")
local fiber = require("fiber")
local proxy_handling = require('proxy_handling')
local log_handling = require('log_handling')
local replication_errors = require("replication_errors")
local fio_utils = require("fio_utils")


_G.SUCCESSFUL_LOGS = os.getenv("ENV_SUCCESSFUL_LOGS") or "false"
_G.WITHOUT_BEST_EFFORT = os.getenv("WITHOUT_BEST_EFFORT") or "false"
_G.WITHOUT_LINEARIZABLE = os.getenv("WITHOUT_LINEARIZABLE") or "false"
_G.WITHOUT_PROXY = os.getenv("WITHOUT_PROXY") or "false"
_G.WORKING_LOG_PATH = os.getenv("WORKING_LOG_PATH") or "./working_log.log"

-- Improved FIFO management
local function setup_fifos()
    os.execute("mkdir -p /tmp/fifo")
    os.execute("rm -f /tmp/fifo/server_in.fifo /tmp/fifo/server_out.fifo")
    os.execute("mkfifo /tmp/fifo/server_in.fifo")
    os.execute("mkfifo /tmp/fifo/server_out.fifo")
end

setup_fifos()

-- Server state management
local STATE = {
    nodes_count = 0,
    logs = "",
    has_error = 0
}

_G.EXTRA_LOGS = function (msg)
    STATE.logs = STATE.logs .. "\n" .. msg
end

_G.error_data = {}

_G.HAS_ERROR = function ()
    STATE.has_error = 1
    fio_utils.add_error_scenario({nodes_count = STATE.nodes_count,operations = _G.error_data.operations, logs = STATE.logs})
end

logger.init_logger()

-- Cluster variables
local CLUSTER = {}
local initial_replication = {}
local is_created = 0

-- Request validation
local function validate_request(request)
    return type(request) == "table" 
        and type(request.path) == "string" 
        and (request.data == nil or type(request.data) == "table")
end

-- Cluster management
local function create_cluster(count)

    STATE.nodes_count = count
    CLUSTER = cluster.make_cluster(count)
    fiber.sleep(20)
    initial_replication = tools.get_initial_replication(CLUSTER.replicas)

    -- Checking the initial configuration
    for _, node in ipairs(CLUSTER.replicas) do
        local node_state = node:exec(function()
            return box.info.election.state
        end)
        LogInfo(string.format("Node %s is %s", node.alias, tostring(node_state)))
        crash_functions.update_node_state(node, "active")
    end

    -- Checking the initial configuration for proxies
    for _, proxy in ipairs(CLUSTER.proxies) do
        local proxy_state = proxy_handling.get_proxy_state(proxy) 
        LogInfo(string.format("Proxy %s is %s", proxy.alias, tostring(proxy_state)))
        crash_functions.update_node_state(proxy, "active")
    end

    -- Finding the leader node
    local leader_node = tools.get_leader(CLUSTER.replicas)
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
        CLUSTER,
        "test",
        1,
        1,
        0.1
    )

    LogInfo("[DIVERGENCE MONITOR] Started")
    log_handling.divergence_monitor(
        CLUSTER,
        "test",
        100,
        1,
        2
    )

    LogInfo("[REPLICATION MONITOR] Started")
    replication_errors.monitor_config = {
        leader_absent_time = 10, 
        max_terms_change_by_period = 3,
        terms_change_period = 20,
        check_interval = 2,
    }
    fiber.create(function(CLUSTER) replication_errors.run_replication_monitor(CLUSTER) end, CLUSTER)

end

-- Operation handling
local function apply_operation(op)
    local available_nodes = crash_functions.get_non_crashed_nodes(CLUSTER.replicas, _G.nodes_activity_states, "replica_")
    local crash_nodes = {}
    local crash_proxies = {}
    table.insert(crash_nodes, crash_functions.get_node_by_id(available_nodes, op.node_1))
    if op.node_2 ~= -1 then
        table.insert(crash_nodes, crash_functions.get_node_by_id(available_nodes,op.node_2))
    end

    if op.crash_type == 2 or op.crash_type == 3 then
        table.insert(crash_proxies, proxy_handling.find_proxy_by_ids(CLUSTER, op.node_1, op.node_2))
        table.insert(crash_proxies, proxy_handling.find_proxy_by_ids(CLUSTER, op.node_2, op.node_1))
    end
    
    -- for _, node in ipairs(crash_nodes) do
    --     print("Node: " .. node.alias.. " state: " .. _G.nodes_activity_states[node.alias])
    --     if not crash_functions.node_is_alive_by_id(CLUSTER , _G.nodes_activity_states , node) then
    --         print("********************************************************************")
    --         return {}
    --     end
    -- end
    if op.crash_type == 3 then
    for _, proxy in ipairs(crash_proxies) do
            if _G.nodes_activity_states[proxy.alias] ~= "crashed" and _G.nodes_activity_states[proxy.alias] ~= "bad_connection" then
                local suc, _ = pcall(crash_functions.emulate_bad_connection,
                    proxy,
                    _G.nodes_activity_states,
                    op.break_duration,
                    op.period,
                    op.total_time
                )

                if not suc then
                    return {}
                end
            end
        end
        fiber.sleep(op.total_time / 10)
    else
        crash_functions.crash_simulation(
            CLUSTER,
            _G.nodes_activity_states,
            initial_replication,
            op.crash_type,
            op.crash_time,
            crash_nodes,
            crash_proxies,
            {}
        )

        fiber.sleep(op.crash_time / 10)
    end
end

-- Request handlers
local function handle_start(data)
    local status = "200"
    local result = { created = is_created }

    if not data then
        status = "400"
        result = { error = "Invalid request data" }
    elseif data.count and type(data.count) == "number" then
        local ok, err = pcall(create_cluster, data.count)
        fiber.sleep(20)
        if ok then
            is_created = 1
            result.created = is_created
        else
            status = "400"
            result.error = tostring(err)
        end
    else
        status = "400"
        result.error = "Missing or invalid count parameter"
    end

    return { status = status, body = result }
end

local function handle_simulate(data)
    _G.error_data = data
    local status = "200"
    local result = {}
    STATE = {
        nodes_count = STATE.nodes_count,
        logs = "",
        has_error = 0
    }
    local max_delay = 0

    if not data then
        status = "400"
        result = { error = "Invalid JSON" }
    
    elseif data.operations and type(data.operations) == "table" then
        print("Received simulation data:" .. json.encode(data))
        for _, op in ipairs(data.operations) do
            local valid = true
            local error_msg = ""

            -- Operation validation
            if (not op.crash_type) or (op.crash_type < 0 or op.crash_type > 3) then
                valid = false
                error_msg = "Invalid crash_type"
            elseif op.crash_type == 2 or op.crash_type == 3 then
                if not op.node_1 or not op.node_2 or op.node_1 == op.node_2 then
                    valid = false
                    error_msg = "Invalid nodes for crash_type " .. op.crash_type
                end
                if op.crash_type == 3 then
                    if not op.break_duration or not op.period or not op.total_time then
                        valid = false
                        error_msg = "Missing parameters for intermittent connection loss"
                    else
                        max_delay = math.max(max_delay, op.total_time)
                    end
                end
            else
                if op.node_2 ~= -1 then
                    valid = false
                    error_msg = "node_2 must be -1 for this crash_type"
                end
            end
            if op.crash_type ~= 3 then
                if (not op.crash_time) or op.crash_time < 0 then
                    valid = false
                    error_msg = "Invalid crash_time"
                else
                    max_delay = math.max(max_delay, op.crash_time)
                end
            end

            if valid then

                local success,err = pcall(apply_operation,op)
                if not success then
                    result = { error = err }
                    status = "400"
                    break
                end
            else
                result = { error = error_msg }
                status = "400"
                break
            end
        end
    else
        status = "400"
        result = { error = "Missing or invalid operations" }
    end
    if status == "200" then
        result = STATE
    end
    -- fiber.sleep(max_delay)
    return { status = status, body = result }
end

-- Main server loop
print("Server running using FIFOs")

while true do
    local infile = io.open("/tmp/fifo/server_in.fifo", "r")
    if infile then
        local line = infile:read("*l")
        infile:close()
        
        if line then
            local ok, request = pcall(json.decode, line)
            local response
            
            if ok and validate_request(request) then
                if request.path == "/start" then
                    response = handle_start(request.data)
                elseif request.path == "/simulate" then
                    response = handle_simulate(request.data)
                else
                    response = { status = "404", body = { error = "Not Found" } }
                end
            else
                response = {
                    status = "400",
                    body = {
                        error = ok and "Invalid request structure" or "Invalid JSON: " .. tostring(request)
                    }
                }
            end

            local outfile = io.open("/tmp/fifo/server_out.fifo", "w")
            if outfile then
                outfile:write(json.encode(response) .. "\n")
                outfile:close()
            end
        end
    end
    fiber.sleep(20)
end
