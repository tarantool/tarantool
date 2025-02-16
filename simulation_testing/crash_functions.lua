local fiber = require('fiber')
local net_box = require('net.box')
local tools = require("tools")

-- Table for tracking the status of nodes
nodes_activity_states = {}
local function update_node_state(node, state)
    nodes_activity_states[node.alias] = state
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
        print("[NODE FILTER] No non-crashed nodes available.")
        return -1
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
        print("[CRASH SIMULATION] Removing " .. num_to_select .. " nodes will make the cluster unhealthy")
        return -1
    end
    
    -- Filtering nodes whose state is not equal to "crashed"
    local available_nodes = get_non_crashed_nodes(nodes, nodes_activity_states)

    if #available_nodes < num_to_select then
        print("[CRASH SIMULATION] Not enough healthy nodes to select")
        return -1
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
local function print_nodes_activity_states()
    print("[NODES ACTIVITY STATES] Current states of nodes:")
    for alias, state in pairs(nodes_activity_states) do
        print(string.format("  - Node: %s, State: %s", alias, state))
    end
end

local function stop_node(node, min_delay, max_delay)
    fiber.create(function()

        tools.check_node(node)

        local delay = tools.calculate_delay(min_delay, max_delay)

        node:stop()
        update_node_state(node, "crashed")
        print(string.format("[CRASH SIMULATION] Node %s is stopped for a time %s", node.alias, delay))
        print_nodes_activity_states()

        fiber.sleep(delay)

        node:start()
        update_node_state(node, "restored")
        print(string.format("[CRASH SIMULATION] Node %s is started again", node.alias))
        print_nodes_activity_states()
    end)
end

local function create_delay_to_write_operations(node, min_delay, max_delay)
    fiber.create(function()

        tools.check_node(node)

        local delay = tools.calculate_delay(min_delay, max_delay)

        node:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY', true)
        end)
        update_node_state(node, "crashed")
        print(string.format("[CRASH SIMULATION] The WAL write delay for node %s is set for the time %d", node.alias, delay))
        print_nodes_activity_states()

        fiber.sleep(delay)

        node:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
        update_node_state(node, "restored")
        print(string.format("[CRASH SIMULATION] The WAL write delay for node %s has been removed", node.alias))
        print_nodes_activity_states()
    end)
end

local function break_connection_between_two_nodes(two_nodes, initial_replication, min_delay, max_delay)
    fiber.create(function()

        local delay = tools.calculate_delay(min_delay, max_delay)

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
        update_node_state(node1, "crashed")

        node2:exec(function(peer)
            local new_replication = {}
            for _, uri in ipairs(box.cfg.replication) do
                if uri ~= peer then
                    table.insert(new_replication, uri)
                end
            end
            box.cfg{replication = new_replication}
        end, {node1.net_box_uri})
        update_node_state(node2, "crashed")

        print(string.format("[CRASH SIMULATION] The connection between nodes %s and %s is broken for %d seconds", node1.alias, node2.alias, delay))
        print_nodes_activity_states()

        fiber.sleep(delay)

        -- Restoring the original replication configuration for first and second nodes
        node1:exec(function(replication)
            box.cfg{replication = replication}
        end, {initial_replication})
        update_node_state(node1, "restored")

        node2:exec(function(replication)
            box.cfg{replication = replication}
        end, {initial_replication})
        update_node_state(node2, "restored")
    
        print(string.format("[CRASH SIMULATION] The connection between nodes %s and %s has been restored", node1.alias, node2.alias))
        print_nodes_activity_states()
    end)
end

local function crash_simulation(cg, nodes_activity_states, initial_replication, lower_crash_time_bound, upper_crash_time_bound, crash_interval)
    fiber.create(function()
        while true do
            fiber.sleep(crash_interval) 
            local type_of_crashing = math.random(1, 3)
            if type_of_crashing == 1 then
                local crash_node = get_random_nodes_for_crash(cg.replicas, nodes_activity_states, 1)
                if crash_node ~= -1 then
                    stop_node(crash_node[1], lower_crash_time_bound, upper_crash_time_bound)
                end

            elseif type_of_crashing == 2 then
                local crash_node = get_random_nodes_for_crash(cg.replicas, nodes_activity_states, 1)
                if crash_node ~= -1 then
                    create_delay_to_write_operations(crash_node[1], lower_crash_time_bound, upper_crash_time_bound)
                end

            else
                local crash_nodes = get_random_nodes_for_crash(cg.replicas, nodes_activity_states, 2)
                if crash_nodes ~= -1 then
                    break_connection_between_two_nodes(crash_nodes, initial_replication, lower_crash_time_bound, upper_crash_time_bound)
                end
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
    crash_simulation = crash_simulation,
    print_nodes_activity_states = print_nodes_activity_states

}