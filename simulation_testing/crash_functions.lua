local fiber = require('fiber')
local net_box = require('net.box')
local my_functions = require("my_functions")

local function stop_node(node, min_delay, max_delay)
    fiber.create(function()

        my_functions.check_node(node)

        local delay = my_functions.calculate_delay(min_delay, max_delay)

        node:stop()
        print(string.format("Node %s is stopped for a time %s", node.alias, delay))
        fiber.sleep(delay)
        print(string.format("Node %s is started again", node.alias))
        node:start()
    end)
end

local function create_delay_to_write_operations(node, space_name, min_delay, max_delay)
    fiber.create(function()

        my_functions.check_node(node)

        local delay = my_functions.calculate_delay(min_delay, max_delay)

        node:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY', true)
        end)
        print(string.format("The WAL write delay for node %s is set for the time %d", node.alias, delay))

        fiber.sleep(delay)

        node:exec(function()
            box.error.injection.set('ERRINJ_WAL_DELAY', false)
        end)
        print(string.format("The WAL write delay for node %s has been removed", node.alias))
    end)
end

local function break_connection_between_random_nodes(nodes, initial_replication, min_delay, max_delay)
    fiber.create(function()

        local delay = my_functions.calculate_delay(min_delay, max_delay)

        -- todo: check conn before disconn
        local node1 = my_functions.get_random_node(nodes)
        local node2 = my_functions.get_random_node(nodes)
        while node1 == node2 do
            node2 = my_functions.get_random_node(nodes)
        end

        local function is_node_ready(node)
            local replication_info = node:exec(function()
                return box.info.replication
            end)
            return replication_info and #replication_info > 0
        end

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

        node2:exec(function(peer)
            local new_replication = {}
            for _, uri in ipairs(box.cfg.replication) do
                if uri ~= peer then
                    table.insert(new_replication, uri)
                end
            end
            box.cfg{replication = new_replication}
        end, {node1.net_box_uri})

        print(string.format("The connection between nodes %s and %s is broken for %d seconds", node1.alias, node2.alias, delay))

        fiber.sleep(delay)

        -- Restoring the original replication configuration for first and second nodes
        node1:exec(function(replication)
            box.cfg{replication = replication}
        end, {initial_replication})

        node2:exec(function(replication)
            box.cfg{replication = replication}
        end, {initial_replication})
    
        print(string.format("The connection between nodes %s and %s has been restored", node1.alias, node2.alias))
    end)
end

return {
    stop_node = stop_node,
    create_delay_to_write_operations = create_delay_to_write_operations,
    break_connection_between_random_nodes = break_connection_between_random_nodes

}