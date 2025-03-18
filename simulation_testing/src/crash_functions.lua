local fiber = require('fiber')

local tools = require("tools")
local proxy_handling = require('proxy_handling')

math.randomseed(os.clock())

-- Table for tracking the status of nodes
nodes_activity_states = {}

nodes_connection_states = {}


local function update_node_state(node_or_proxy, state_status)
    nodes_activity_states[node_or_proxy.alias] = state_status
end

local function update_node_connection_state(node1,node2, state)
    nodes_connection_states[node1.alias..node2.alias] = state
    nodes_connection_states[node2.alias..node1.alias] = state
end

local function is_node_alive_by_id(replica_id)
    return nodes_activity_states["replica_"..tostring(replica_id)] ~= 'crashed'
end


local function is_node_alive_by_alias(node)
    return nodes_activity_states[node.alias] ~= 'crashed'
end


local function connection_exists(node1_id, node2_id)
    return nodes_connection_states['replica_'..tostring(node1_id)..'replica_'..tostring(node2_id)] == true
end

-- The function of getting all workable nodes
local function get_non_crashed_nodes(nodes, nodes_activity_states)
    local non_crashed_nodes = {}

    for _, node in ipairs(nodes) do
        if nodes_activity_states[node.alias] ~= "crashed" then
            table.insert(non_crashed_nodes, node)
        end
    end

    if #non_crashed_nodes == 0 then
        LogInfo("[NODE FILTER] No non-crashed nodes available.")
        return {}
    end

    return non_crashed_nodes
end

local function count_crashed_nodes(nodes_activity_states)
    local crashed_count = 0

    for _, state in pairs(nodes_activity_states) do
        if state == "crashed" then
            crashed_count = crashed_count + 1
        end
    end

    return crashed_count
end

-- Checking the condition for a sufficient number of workable nodes
--- N is the total number of nodes in the cluster
--- num_crashed_nodes is the number of crashed nodes in the cluster
local function is_cluster_healthy(N, num_crashed_nodes)
    local min_num_non_crashed_nodes = math.floor(N / 2) + 1
    return (N - num_crashed_nodes) >= min_num_non_crashed_nodes
end

-- Safe function for getting random crash nodes
local function get_random_nodes_for_crash(nodes, nodes_activity_states, num_to_select)

    -- Checking that the limit on the number of active nodes in the cluster is not violated
    local prev_num_crashed_nodes = count_crashed_nodes(nodes_activity_states)
    local new_num_crashed_nodes = prev_num_crashed_nodes + num_to_select
    if not is_cluster_healthy(#nodes, new_num_crashed_nodes) then
        LogInfo("[CRASH SIMULATION] Removing " .. num_to_select .. " nodes will make the cluster unhealthy")
        return {}
    end
    
    -- Filtering nodes whose state is not equal to "crashed"
    local available_nodes = get_non_crashed_nodes(nodes, nodes_activity_states)

    if #available_nodes < num_to_select then
        LogInfo("[CRASH SIMULATION] Not enough healthy nodes to select")
        return {}
    end

    -- Randomly select nodes
    local selected_nodes = {}
    while #selected_nodes < num_to_select do
        local index = math.random(#available_nodes)
        local node = table.remove(available_nodes, index) 
        table.insert(selected_nodes, node) 
    end

    return selected_nodes
end


-- For the convenience of logging and debugging node states
local function LogInfo_nodes_activity_states()
    LogInfo("[NODES ACTIVITY STATES] Current states of nodes and proxies:")

    local function extract_id(alias)
        return tonumber(alias:match("%d+")) 
    end

    local nodes = {}
    local proxies = {}

    for alias, state in pairs(nodes_activity_states) do
        if alias:startswith("replica_") then
            table.insert(nodes, {alias = alias, state = state, id = extract_id(alias)})
        elseif alias:startswith("proxy_") then
            table.insert(proxies, {alias = alias, state = state, id = extract_id(alias)})
        end
    end

    table.sort(nodes, function(a, b) return a.id < b.id end)
    table.sort(proxies, function(a, b) return a.id < b.id end)

    LogInfo("Nodes:")
    for _, node in ipairs(nodes) do
        LogInfo(string.format("  - Node: %s, State: %s", node.alias, node.state))
    end

    LogInfo("Proxies:")
    for _, proxy in ipairs(proxies) do
        LogInfo(string.format("  - Proxy: %s, State: %s", proxy.alias, proxy.state))
    end
end

local function stop_node(node, delay)
    fiber.create(function()

        tools.check_node(node)


        node:stop()
        update_node_state(node, "crashed")
        LogInfo(string.format("[CRASH SIMULATION] Node %s is stopped for a time %s", node.alias, delay))
        LogInfo_nodes_activity_states()

        fiber.sleep(delay)

        node:start()
        update_node_state(node, "restored")
        LogInfo(string.format("[CRASH SIMULATION] Node %s is started again", node.alias))
        LogInfo_nodes_activity_states()
    end)
end

local function pause_proxy(proxy, delay)
    fiber.create(function()
        proxy:pause()
        update_node_state(proxy, "crashed")
        LogInfo(string.format("[CRASH SIMULATION] Proxy for node %s is paused for a time %s", proxy.alias, delay))
        LogInfo_nodes_activity_states()

        fiber.sleep(delay)

        proxy:resume()
        update_node_state(proxy, "restored")
        LogInfo(string.format("[CRASH SIMULATION] Proxy for node %s is resumed", proxy.alias))
        LogInfo_nodes_activity_states()
    end)
end

local function create_delay_to_write_operations(node, delay)
    fiber.create(function()

        tools.check_node(node)


        node:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY', true)
        end)
        update_node_state(node, "crashed")
        LogInfo(string.format("[CRASH SIMULATION] The WAL write delay for node %s is set for the time %d", node.alias, delay))
        LogInfo_nodes_activity_states()

        fiber.sleep(delay)

        node:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
        update_node_state(node, "restored")
        LogInfo(string.format("[CRASH SIMULATION] The WAL write delay for node %s has been removed", node.alias))
        LogInfo_nodes_activity_states()
    end)
end

local function break_connection_between_two_nodes(two_nodes, initial_replication, delay)
    fiber.create(function()


        local function is_node_ready(node)
            local replication_info = node:exec(function()
                return box.info.replication
            end)
            return replication_info and #replication_info > 0
        end

        local node1 = two_nodes[1]
        local node2 = two_nodes[2]

        if not is_node_ready(node1) then
            error(string.format("[CRASH SIMULATION] Node %s is not connected or is not replicated", node1.alias))
        end

        if not is_node_ready(node2) then
            error(string.format("[CRASH SIMULATION] Node %s is not connected or is not replicated", node2.alias))
        end

        -- Replication Break Operations
        node1:exec(function(peer)
            local new_replication = {}
            for _, uri in ipairs(box.cfg.replication) do
                if uri ~= peer then
                    table.insert(new_replication, uri)
                end
            end
            box.cfg{replication = new_replication}
        end, {node2.net_box_uri})

        node2:exec(function(peer)
            local new_replication = {}
            for _, uri in ipairs(box.cfg.replication) do
                if uri ~= peer then
                    table.insert(new_replication, uri)
                end
            end
            box.cfg{replication = new_replication}
        end, {node1.net_box_uri})
        update_node_connection_state(node1, node2, false)

        LogInfo(string.format("[CRASH SIMULATION] The connection between nodes %s and %s is broken for %d seconds", node1.alias, node2.alias, delay))
        LogInfo_nodes_activity_states()

        fiber.sleep(delay)

        -- Restoring the original replication configuration for first and second nodes
        node1:exec(function(replication)
            box.cfg{replication = replication}
        end, {initial_replication})
        update_node_state(node1, "restored")

        node2:exec(function(replication)
            box.cfg{replication = replication}
        end, {initial_replication})
        update_node_connection_state(node1, node2, false)
    
        LogInfo(string.format("[CRASH SIMULATION] The connection between nodes %s and %s has been restored", node1.alias, node2.alias))
        LogInfo_nodes_activity_states()
    end)
end

local function crash_simulation(cg, nodes_activity_states, initial_replication, type_of_crashing, delay, crash_nodes ,crashed_proxy_nodes)
    if type_of_crashing == 1 then
        if #crash_nodes > 0 then
            local success, err = pcall(stop_node, crash_nodes[1], delay)
            if not success then
                LogInfo(string.format("[CRASH SIMULATION] Error: Failed to stop node: %s", err))
            end
        end

    elseif type_of_crashing == 2 then
        if #crash_nodes > 0 then
            local success, err = pcall(create_delay_to_write_operations, crash_nodes[1], delay)
            if not success then
                LogInfo(string.format("[CRASH SIMULATION] Error: Failed to create delay for node: %s", err))
            end
        end

    elseif type_of_crashing == 3 then
        if #crash_nodes > 0 then
            local success, err = pcall(break_connection_between_two_nodes, crash_nodes, initial_replication, delay)
            if not success then
                LogInfo(string.format("[CRASH SIMULATION] Error: Failed to break connection between nodes: %s", err))
            end
        end

    elseif type_of_crashing == 4 then
        
        if #crashed_proxy_nodes > 0 then
            for i, proxy in ipairs(crashed_proxy_nodes) do
                local success, err = pcall(pause_proxy, proxy, delay)
                if not success then
                    LogInfo(string.format("[CRASH SIMULATION] Error: Failed to pause proxy %d: %s", i, err))
                end
            end
        end
    end

end
local function random_crash_simulation(cg, nodes_activity_states, initial_replication, lower_crash_time_bound, upper_crash_time_bound, crash_interval)
    fiber.create(function()
        while true do
            local success, err = pcall(function()
                fiber.sleep(crash_interval)

                local type_of_crashing = math.random(1, 4)
                local delay = tools.calculate_delay(lower_crash_time_bound, upper_crash_time_bound)
                local crash_node = get_random_nodes_for_crash(cg.replicas, nodes_activity_states, 1)
                local crashed_proxy_nodes = proxy_handling.get_random_proxies_for_crash(cg, nodes_activity_states, 1)  -- at first time, only 1 proxy
                crash_simulation(cg, nodes_activity_states, initial_replication, type_of_crashing, delay, crash_node, crashed_proxy_nodes)
            end)
            if not success then
                LogInfo(string.format("[CRASH SIMULATION] Error in crash_simulation: %s", err))
            end
        end
    end)
end

return {
    update_node_state = update_node_state,
    get_non_crashed_nodes = get_non_crashed_nodes,
    count_crashed_nodes = count_crashed_nodes,
    is_cluster_healthy = is_cluster_healthy,
    get_random_nodes_for_crash = get_random_nodes_for_crash,
    stop_node = stop_node,
    create_delay_to_write_operations = create_delay_to_write_operations,
    break_connection_between_two_nodes = break_connection_between_two_nodes,
    random_crash_simulation = random_crash_simulation,
    LogInfo_nodes_activity_states = LogInfo_nodes_activity_states,
    is_node_alive_by_id = is_node_alive_by_id,
    connection_exists = connection_exists,
    is_node_alive_by_alias = is_node_alive_by_alias,
    crash_simulation = crash_simulation,
}