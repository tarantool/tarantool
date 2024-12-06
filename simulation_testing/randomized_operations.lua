local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fiber = require('fiber')
local net_box = require('net.box')
local my_functions = require("my_functions")
local crash_functions = require("crash_functions")

-- todo: operations with transactions
-- todo: accounting for space parameters
local function generate_random_operation(space, max_key, is_read_only)

    local rnd_key
    local rnd_value
    local operations
    if is_read_only then
        operations = {"select", "get"}
        rnd_key = math.random(1, max_key) 
    else
        operations = {"select", "insert", "replace", "update", "upsert", "delete", "get", "put"}
        rnd_key = math.random(1, max_key) 
        rnd_value = "Value_" .. rnd_key  
    end
    local operation = operations[math.random(#operations)]


    local tuple
    if my_functions.contains({"insert", "replace", "upsert", "put"}, operation) then
        tuple = {rnd_key, rnd_value} 
    elseif my_functions.contains({"select", "get","delete"}, operation) then
        tuple = {rnd_key}
    elseif my_functions.contains({"update"}, operation) then
        tuple = {rnd_key, operations_list}
    end

    return {operation, tuple}
end


local function do_random_operation(node, space, max_key)

    my_functions.check_node(node)

    fiber.create(function()
        local uri = node.net_box_uri
        print(string.format("Подключение к ноде: %s", uri))
        
        local conn = require('net.box').connect(uri)
        if not conn then
            print("Ошибка подключения к ноде:", uri)
            return
        end
    
        local space = conn.space.test
        if not space then
            print("Ошибка: space не найден на ноде:", uri)
            conn:close()
            return
        end

        generated = generate_random_operation(space, max_key, my_functions.is_follower(conn))
        local operation = generated[1]
        local tuple = generated[2]
        local message = ""

        if operation == "select" then
            
            local success, result = pcall(function()
                box.begin({txn_isolation  = 'linearizable'})
                local res = space:select(tuple[1])
                box.commit()  
                return res
            end)
            if success then
                if #result == 0 then
                    message = "Записи не найдены для ключа " .. tostring(tuple[1])
                else
                    message = "Результаты select для ключа " .. tostring(tuple[1]) .. ":\n"
                    for _, row in ipairs(result) do
                        message = message .. " " .. tostring(row)
                    end
                end
            else
                message = "Ошибка при выполнении select: " .. tostring(result)
            end

        elseif operation == "insert" then
            
            local success, err = pcall(function()
                box.begin({txn_isolation  = 'linearizable'})
                space:insert(tuple)
                box.commit()
            end)
            if success then
                message = "Запись успешно вставлена: " .. my_functions.table_to_string(tuple)
            else
                message = "Ошибка при выполнении insert: " .. tostring(err)
            end

        elseif operation == "replace" then
            
            local success, err = pcall(function()
                box.begin({txn_isolation  = 'linearizable'})
                space:replace(tuple)
                box.commit()
            end)
            if success then
                message = "Запись успешно заменена: " .. my_functions.table_to_string(tuple)
            else
                message = "Ошибка при выполнении replace: " .. tostring(err)
            end

        elseif operation == "update" then
            
            local update_op = {{'=', 2, tuple[2]}}  -- mock operation
            local success, err = pcall(function()
                box.begin({txn_isolation  = 'linearizable'})
                space:update(tuple[1], update_op)
                box.commit()
            end)
            if success then
                message = "Запись успешно обновлена " .. my_functions.table_to_string(tuple)
            else
                message = "Ошибка при выполнении update: " .. tostring(err)
            end

        elseif operation == "upsert" then
            
            local update_op = {{'=', #tuple, tuple[2]}}  -- mock operation
            local success, err = pcall(function()
                box.begin({txn_isolation  = 'linearizable'})
                space:upsert(tuple, update_op)  
                box.commit()
            end)
            if success then
                message = "Запись успешно вставлена или обновлена: " .. my_functions.table_to_string(tuple)
            else
                message = "Ошибка при выполнении upsert: " .. tostring(err)
            end

        elseif operation == "delete" then
            
            local success, err = pcall(function()
                box.begin({txn_isolation  = 'linearizable'})
                space:delete(tuple[1])
                box.commit()
            end)
            if success then
                message = "Запись успешно удалена для ключа " .. tostring(tuple[1])
            else
                message = "Ошибка при выполнении delete: " .. tostring(err)
            end

        elseif operation == "get" then
            
            local success, result = pcall(function()
                box.begin({txn_isolation  = 'linearizable'})
                local res = space:get(tuple[1])
                box.commit()  
                return res
            end)
            if success then
                if result then
                    message = "Результат get для ключа " .. tostring(tuple[1]) .. ": " .. tostring(result)
                else
                    message = "Запись не найдена для ключа " .. tostring(tuple[1])
                end
            else
                message = "Ошибка при выполнении get:" .. tostring(result)
            end

        elseif operation == "put" then
            
            local success, err = pcall(function()
                box.begin({txn_isolation  = 'linearizable'})
                space:replace(tuple)
                box.commit()
            end)
            if success then
                message = "Запись " .. my_functions.table_to_string(tuple) .. " успешно вставлена или обновлена для ключа " .. tostring(tuple[1])
            else
                message = "Ошибка при выполнении put:" .. tostring(err)
            end
        end

        conn:close()
        print(message)
    end)
end

return {
    generate_random_operation = generate_random_operation,
    do_random_operation = do_random_operation

}




