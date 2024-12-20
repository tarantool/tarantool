local fiber = require('fiber')
local net_box = require('net.box')
local my_functions = require("my_functions")

-- A table for tracking the status of nodes
nodes_activity_states = {}
local function update_node_state(node, state)
    nodes_activity_states[node.alias] = state
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

local function is_cluster_healthy(nodes_activity_states, total_nodes)
    local crashed_count = count_crashed_nodes(nodes_activity_states)
    local min_num_active_nodes = math.floor(total_nodes / 2) + 1
    return (total_nodes - crashed_count) >= min_num_active_nodes
end

local function get_random_nodes_for_crash(nodes, nodes_activity_states, num_to_select)

    -- Checking that the limit on the number of active nodes in the cluster is not violated
    local remaining_nodes = #nodes - num_to_select
    if not is_cluster_healthy(nodes_activity_states, remaining_nodes) then
        print("Removing " .. num_to_select .. " nodes will make the cluster unhealthy")
        return -1
    end
    
    -- Filtering nodes whose state is not equal to "crashed"
    local available_nodes = {}
    for _, node in ipairs(nodes) do
        if nodes_activity_states[node.alias] ~= "crashed" then
            table.insert(available_nodes, node)
        end
    end

    if #available_nodes < num_to_select then
        print("Not enough healthy nodes to select")
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


local function stop_node(node, min_delay, max_delay)
    fiber.create(function()

        my_functions.check_node(node)

        local delay = my_functions.calculate_delay(min_delay, max_delay)

        node:stop()
        update_node_state(node, "crashed")
        print(string.format("Node %s is stopped for a time %s", node.alias, delay))

        fiber.sleep(delay)

        node:start()
        update_node_state(node, "restored")
        print(string.format("Node %s is started again", node.alias))
    end)
end

local function create_delay_to_write_operations(node, space_name, min_delay, max_delay)
    fiber.create(function()

        my_functions.check_node(node)

        local delay = my_functions.calculate_delay(min_delay, max_delay)

        node:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY', true)
        end)
        update_node_state(node, "crashed")
        print(string.format("The WAL write delay for node %s is set for the time %d", node.alias, delay))

        fiber.sleep(delay)

        node:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
        update_node_state(node, "restored")
        print(string.format("The WAL write delay for node %s has been removed", node.alias))
    end)
end

local function break_connection_between_two_nodes(two_nodes, initial_replication, min_delay, max_delay)
    fiber.create(function()

        local delay = my_functions.calculate_delay(min_delay, max_delay)

        local function is_node_ready(node)
            local replication_info = node:exec(function()
                return box.info.replication
            end)
            return replication_info and #replication_info > 0
        end

        local node1 = two_nodes[1]
        local node2 = two_nodes[2]

        if not is_node_ready(node1) then
            error(string.format("Node %s is not connected or is not replicated", node1.alias))
        end

        if not is_node_ready(node2) then
            error(string.format("Node %s is not connected or is not replicated", node2.alias))
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

        print(string.format("The connection between nodes %s and %s is broken for %d seconds", node1.alias, node2.alias, delay))

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
    
        print(string.format("The connection between nodes %s and %s has been restored", node1.alias, node2.alias))
    end)
end

return {
    update_node_state = update_node_state,
    count_crashed_nodes = count_crashed_nodes,
    is_cluster_healthy = is_cluster_healthy,
    get_random_nodes_for_crash = get_random_nodes_for_crash,
    stop_node = stop_node,
    create_delay_to_write_operations = create_delay_to_write_operations,
    break_connection_between_two_nodes = break_connection_between_two_nodes

}