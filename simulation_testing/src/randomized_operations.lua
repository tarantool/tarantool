local fiber = require('fiber')
local tools = require("tools")





local function generate_random_read_operation(max_key)
    local operation_types = {"select", "get"}
    local rnd_key = math.random(1, max_key)

    local operation_type = operation_types[math.random(#operation_types)]
    local operation_args = {rnd_key}

    return {operation_type, operation_args}
end

local function generate_random_write_operation(max_key)
    local operation_types = {"insert", "replace", "update", "upsert", "delete", "put"}
    local rnd_key = math.random(1, max_key)
    local rnd_value = "Value for key " .. rnd_key

    local operation_type = operation_types[math.random(#operation_types)]
    local operation_args

    if tools.contains({"insert", "replace", "upsert", "put"}, operation_type) then
        operation_args = {rnd_key, rnd_value}
    elseif tools.contains({"delete"}, operation_type) then
        operation_args = {rnd_key} 
    elseif tools.contains({"update"}, operation_type) then
        operation_args = {rnd_key, rnd_value}
    end

    return {operation_type, operation_args}
end

local function execute_db_operation(node, space_name, operation)
    tools.check_node(node)

    fiber.create(function()
        local operation_type = operation[1]
        local operation_args = operation[2]
        local message = ""
        local node_name = node.alias

        local success, result = pcall(function()
            return node:exec(function(operation_type, operation_args, space_name)
                
                local space = box.space[space_name]
                local res

                local txn_opts = {}
                if WITHOUT_LINEARIZABLE ~= "true" then
                    txn_opts = { txn_isolation = 'linearizable' }
                end

                box.begin(txn_opts)
                if operation_type == "select" then
                    res = space:select(operation_args[1])
                elseif operation_type == "insert" then
                    space:insert(operation_args)
                elseif operation_type == "replace" then
                    space:replace(operation_args)
                elseif operation_type == "update" then
                    local update_op = {{'=', 2, operation_args[2]}} -- mock operation
                    space:update(operation_args[1], update_op)
                elseif operation_type == "upsert" then
                    local update_op = {{'=', #operation_args, operation_args[2]}} -- mock operation
                    space:upsert(operation_args, update_op)
                elseif operation_type == "delete" then
                    space:delete(operation_args[1])
                elseif operation_type == "get" then
                    res = space:get(operation_args[1])
                elseif operation_type == "put" then
                    space:replace(operation_args)
                end

                box.commit()
                return res
            end, {operation_type, operation_args, space_name})
        end)

        if success then
            if operation_type == "select" or operation_type == "get" then
                if result then
                    message = string.format(
                        "Operation: %s, Args: %s, Result: %s, Node: %s, Space: %s",
                        operation_type,
                        tostring(operation_args[1]),
                        tools.table_to_string(result), 
                        node_name, 
                        space_name
                )
                else
                    message = string.format(
                        "Operation: %s, Args: %s, Result: No entries found, Node: %s, Space: %s",
                        operation_type,
                        tostring(operation_args[1]),
                        node_name, 
                        space_name
                    )
                end
            else
                message = string.format(
                    "Operation: %s, Args: %s, Result: Completed successfully, Node: %s, Space: %s",
                    operation_type,
                    tools.table_to_string(operation_args),
                    node_name, 
                    space_name
                )
            end
        else
            message = string.format(
                "Error while executing the DB operation. Operation: %s, Args: %s, Node: %s, Space: %s, Error(result): %s",
                operation_type,
                tools.table_to_string(operation_args),
                node_name,
                space_name,
                tools.table_to_string(result)
            )
        end

        print(message)
    end)
end

return {
    generate_random_read_operation = generate_random_read_operation,
    generate_random_write_operation = generate_random_write_operation,
    execute_db_operation = execute_db_operation

}




