local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fiber = require('fiber')
local net_box = require('net.box')
local my_functions = require("my_functions")
local crash_functions = require("crash_functions")



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

    if my_functions.contains({"insert", "replace", "upsert", "put"}, operation_type) then
        operation_args = {rnd_key, rnd_value}
    elseif my_functions.contains({"delete"}, operation_type) then
        operation_args = {rnd_key} 
    elseif my_functions.contains({"update"}, operation_type) then
        operation_args = {rnd_key, rnd_value}
    end

    return {operation_type, operation_args}
end

local function execute_db_operation(node, space_name, operation)
    my_functions.check_node(node)

    fiber.create(function()
        local operation_type = operation[1]
        local operation_args = operation[2]
        local message = ""

        local success, result = pcall(function()
            return node:exec(function(operation_type, operation_args, space_name)
                box.begin({txn_isolation = 'linearizable'})
                local space = box.space[space_name]
                local res

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
                    message = "Results " .. operation_type .. " for the key " .. tostring(operation_args[1]) .. ": " .. my_functions.table_to_string(result)
                else
                    message = "No entries found for the key " .. tostring(operation_args[1])
                end
            else
                message = "Operation " .. operation_type .. " completed successfully: " .. my_functions.table_to_string(operation_args)
            end
        else
            message = "Error while executing the operation " .. operation_type .. ": " .. tostring(result)
        end

        print(message)
    end)
end

return {
    generate_random_read_operation = generate_random_read_operation,
    generate_random_write_operation = generate_random_write_operation,
    execute_db_operation = execute_db_operation

}




