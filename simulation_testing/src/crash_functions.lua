local fiber = require('fiber')

local tools = require("tools")
local proxy_handling = require('proxy_handling')
local string = require('string')

math.randomseed(os.clock())

-- Table for tracking the status of nodes
_G.nodes_activity_states = {}


_G.nodes_connection_states = {}


local lock_ch = fiber.channel(1)
lock_ch:put(true)

local function lock()
    lock_ch:get()  -- когда «токена» в канале нет, вызов заблокируется
end

local function unlock()
    lock_ch:put(true)  -- вернём «токен» в канал, разблокируем «очередь»
end



local function update_node_state(node_or_proxy, state_status)
    lock()
    _G.nodes_activity_states[node_or_proxy.alias] = state_status
    unlock()
end

local function update_node_connection_state(node1,node2, state)
    _G.nodes_connection_states[node1.alias..node2.alias] = state
    _G.nodes_connection_states[node2.alias..node1.alias] = state
end

local function is_node_alive_by_id(replica_id)
    return _G.nodes_activity_states["replica_"..tostring(replica_id)] ~= 'crashed'
end


local function is_node_alive_by_alias(node)
    return _G.nodes_activity_states[node.alias] ~= 'crashed'
end


local function connection_exists(node1_id, node2_id)
    return _G.nodes_connection_states['replica_'..tostring(node1_id)..'replica_'..tostring(node2_id)] == true
end

local function get_node_by_id(nodes, node_id)
    for _, node in pairs(nodes) do
        if node.alias == "replica_".. tostring(node_id) then
            return node
        end
    end
    return nil
end



-- The function of getting all workable nodes
local function get_non_crashed_nodes(nodes, nodes_activity_states, prefix)
    local non_crashed_nodes = {}

    for _, node in ipairs(nodes) do
        if node.alias:startswith(prefix) == true and nodes_activity_states[node.alias] ~= "crashed" then
            table.insert(non_crashed_nodes, node)
        end
    end

    if #non_crashed_nodes == 0 then
        LogInfo("[NODE FILTER] No non-crashed nodes available.")
        return {}
    end

    return non_crashed_nodes
end

local function node_is_alive_by_id(cg,nodes_activity_states, node_id)
    local available_nodes = get_non_crashed_nodes(cg.replicas, nodes_activity_states, "replica")
    if get_node_by_id(available_nodes, node_id) == nil then
        return false
    end
    return true
end

local function count_crashed_nodes(nodes_activity_states, alias_prefix)
    local crashed_count = 0

    for alias, state in pairs(nodes_activity_states) do
        if state == "crashed" and alias:startswith(alias_prefix) == true then
            crashed_count = crashed_count + 1
        end
    end

    return crashed_count
end

-- Checking the condition for a sufficient number of workable nodes
--- N is the total number of nodes in the cluster
--- num_crashed_nodes is the number of crashed nodes in the cluster
local function is_cluster_healthy(N, num_crashed_nodes, node_type)
    local min_num_non_crashed_nodes
    if node_type == "replica" then
            min_num_non_crashed_nodes = math.floor(N / 2) + 1
    else
            min_num_non_crashed_nodes = math.floor(N / 2)
    end
    return (N - num_crashed_nodes) >= min_num_non_crashed_nodes
end


local function is_this_node_crash_safe(nodes, nodes_activity_states, num_to_select)
    lock()
    -- Checking that the limit on the number of active nodes in the cluster is not violated
    local prev_num_crashed_nodes = count_crashed_nodes(nodes_activity_states, "replica_")
    local new_num_crashed_nodes = prev_num_crashed_nodes + num_to_select
    if not is_cluster_healthy(#nodes, new_num_crashed_nodes, "replica") then
        LogInfo("[CRASH SIMULATION] Removing " .. num_to_select .. " nodes will make the cluster unhealthy")
        unlock()
        return false
    end
    
    -- Filtering nodes whose state is not equal to "crashed"
    local available_nodes = get_non_crashed_nodes(nodes, nodes_activity_states, "replica_")
    if #available_nodes < num_to_select then
        LogInfo("[CRASH SIMULATION] Not enough healthy nodes to select")
        unlock()
        return false
    end
    unlock()
    return true
end

local function is_this_proxy_crash_safe(nodes, nodes_activity_states, num_to_select)
    lock()
    -- Checking that the limit on the number of active nodes in the cluster is not violated
    local prev_num_crashed_nodes = count_crashed_nodes(nodes_activity_states, "proxy_")
    local new_num_crashed_nodes = prev_num_crashed_nodes + num_to_select
    if not is_cluster_healthy(#nodes, new_num_crashed_nodes,"proxy") then
        LogInfo("[CRASH SIMULATION] Breaking connection will make the cluster unhealthy")
        unlock()
        return false
    end
    
    -- Filtering nodes whose state is not equal to "crashed"
    local available_nodes = get_non_crashed_nodes(nodes, nodes_activity_states, "proxy_")
    if #available_nodes < num_to_select then
        LogInfo("[CRASH SIMULATION] Not enough healthy connections to break")
        unlock()
        return false
    end
    unlock()
    return true
end

-- Safe function for getting random crash nodes
local function get_random_nodes_for_crash(nodes, nodes_activity_states, num_to_select)
    local available_nodes = get_non_crashed_nodes(nodes, nodes_activity_states, "replica_")

    if (is_this_node_crash_safe(nodes, nodes_activity_states, num_to_select) == false) then
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

    for alias, state in pairs(_G.nodes_activity_states) do
        if alias:startswith("replica_") then
            table.insert(nodes, {alias = alias, state = state, id = extract_id(alias)})
        elseif alias:startswith("proxy_") and WITHOUT_PROXY ~= "true" then
            table.insert(proxies, {alias = alias, state = state, id = extract_id(alias)})
        end
    end

    table.sort(nodes, function(a, b) return a.id < b.id end)
    
    LogInfo("Nodes:")
    for _, node in ipairs(nodes) do
        LogInfo(string.format("  - Node: %s, State: %s", node.alias, node.state))
    end

    if WITHOUT_PROXY ~= "true" then
        table.sort(proxies, function(a, b) return a.id < b.id end)
        
        LogInfo("Proxies:")
        for _, proxy in ipairs(proxies) do
            LogInfo(string.format("  - Proxy: %s, State: %s", proxy.alias, proxy.state))
        end
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
        -- update_node_state(node, "crashed")
        LogInfo(string.format("[CRASH SIMULATION] The WAL write delay for node %s is set for the time %d", node.alias, delay))
        LogInfo_nodes_activity_states()

        fiber.sleep(delay)

        node:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
        -- update_node_state(node, "restored")
        LogInfo(string.format("[CRASH SIMULATION] The WAL write delay for node %s has been removed", node.alias))
        LogInfo_nodes_activity_states()
    end)
end

local function break_connection_between_two_nodes(two_nodes, initial_replication, delay)
    fiber.create(function()

        if not two_nodes or #two_nodes < 2 then
            error("[CRASH SIMULATION] Invalid nodes provided: two_nodes must contain exactly 2 nodes")
        end

        local function is_node_ready(node)
            local replication_info = node:exec(function()
                return box.info.replication
            end)
            return replication_info and #replication_info > 0
        end

        local node1 = two_nodes[1]
        local node2 = two_nodes[2]

        if not node1 or not node2 then
            error("[CRASH SIMULATION] Invalid nodes: one or both nodes are nil")
        end

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
    local available_nodes = get_non_crashed_nodes(cg.replicas, nodes_activity_states, "replica_")
    if type_of_crashing == 1 then
        if is_this_node_crash_safe(cg.replicas,nodes_activity_states, #crash_nodes) == false then
            return {}
        end
        if #crash_nodes > 0 then

            local node = crash_nodes[1]
            if node == nil then
                LogInfo("[CRASH SIMULATION] Node is not available")
                return
            end
            local success, err = pcall(stop_node, node, delay)
            if not success then
                LogInfo(string.format("[CRASH SIMULATION] Error: Failed to stop node: %s", err))
            end
        else
            LogInfo("[CRASH SIMULATION] No Nodes to crash")
        end
    elseif type_of_crashing == 2 then
        if #crash_nodes > 0 then
            local node = crash_nodes[1]
            if node == nil then
                LogInfo("[CRASH SIMULATION] Node with id "..tostring(crash_nodes[1]).." is not available")
                return
            end
            local success, err = pcall(create_delay_to_write_operations, node, delay)
            if not success then
                LogInfo(string.format("[CRASH SIMULATION] Error: Failed to create delay for node: %s", err))
            end
        end

    elseif type_of_crashing == 3 then
        if #crash_nodes > 0 then
            local node_1 = crash_nodes[1]
            local node_2 = crash_nodes[2]

            if node_1 == nil  or  node_2 == nil then
                LogInfo("[CRASH SIMULATION] Node is not available")
                return
            end

            local success, err = pcall(function()
                local crash_nodes_ = { node_1, node_2 }
                break_connection_between_two_nodes(crash_nodes_, initial_replication, delay)
            end)

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
                local crash_node_nodes
                if type_of_crashing == 3 then
                    crash_node_nodes = get_random_nodes_for_crash(cg.replicas, nodes_activity_states, 2)
                else
                    crash_node_nodes = get_random_nodes_for_crash(cg.replicas, nodes_activity_states, 1)
                end
                
                local crashed_proxy_nodes = {}
                if WITHOUT_PROXY ~= "true" then
                    crashed_proxy_nodes = proxy_handling.get_random_proxies_for_crash(cg, nodes_activity_states, 1) -- at first time, only 1 proxy
                end
                -- error in two nodes
                crash_simulation(cg, nodes_activity_states, initial_replication, type_of_crashing, delay, crash_node_nodes, crashed_proxy_nodes)
            end)
            if not success then
                LogError(string.format("[CRASH SIMULATION] Error in crash_simulation: %s", err))
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
    node_is_alive_by_id = node_is_alive_by_id,
    is_this_node_crash_safe = is_this_node_crash_safe,
    is_this_proxy_crash_safe = is_this_proxy_crash_safe,
}