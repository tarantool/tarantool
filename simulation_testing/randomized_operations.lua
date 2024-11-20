local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fiber = require('fiber')
local net_box = require('net.box')

-- Starting and configuring a cluster
local cg = {}
cg.cluster = cluster:new()
cg.nodes = {}

local box_cfg = {
    election_mode = 'candidate', 
    replication_timeout = 0.1,    
    replication = {
        server.build_listen_uri('node1', cg.cluster.id),
        server.build_listen_uri('node2', cg.cluster.id),
        server.build_listen_uri('node3', cg.cluster.id),
    }      
}

cg.nodes[1] = cg.cluster:build_and_add_server({
    alias = 'node1',
    box_cfg = box_cfg,
})
cg.nodes[2] = cg.cluster:build_and_add_server({
    alias = 'node2',
    box_cfg = box_cfg,
})
cg.nodes[3] = cg.cluster:build_and_add_server({
    alias = 'node3',
    box_cfg = box_cfg,
})

cg.cluster:start()

-- Checking the initial configuration
for _, node in ipairs(cg.nodes) do
    local node_state = node:exec(function()
        return box.info.election.state
    end)
    print(string.format("Node %s is %s", node.alias, tostring(node_state)))
end

-- Finding the leader node
local leader_node = nil
for _, node in ipairs(cg.nodes) do
    local node_state = node:exec(function()
        return box.info.election.state
    end)
    if node_state == "leader" then
        leader_node = node
        print(string.format("Leader found: %s", node.alias))
        break
    end
end

if not leader_node then
    error("The leader has not been found. Make sure that replication and elections are configured!!!")
end

--Creating a test space
local result = leader_node:exec(function()
    local message = ""

    if box.cfg == nil then
        error("box.cfg{} was not called, the configuration is missing!!!")
    end

    if not box.space.test then
        local space = box.schema.create_space('test', {
            format = {
                {name = 'id', type = 'unsigned'},
                {name = 'value', type = 'string'},
            }
        })
        box.space.test:truncate()
        space:create_index('primary', {parts = {'id'}})
        message = "The 'test' space is created and configured on the leader"
    else
        message = "The 'test' space has already been previously created and configured on the callout"
    end
    return message
end)

print(result)

-- List search function
local function contains(tbl, value)
    for _, v in ipairs(tbl) do
        if v == value then
            return true
        end
    end
    return false
end

-- For the convenience of displaying tables
local function table_to_string(tbl)
    if type(tbl) ~= "table" then
        return tostring(tbl)
    end
    local result = {}
    for _, v in ipairs(tbl) do
        table.insert(result, tostring(v))
    end
    return "{" .. table.concat(result, ", ") .. "}"
end

local function is_follower(conn)
    return conn:call('box.info').ro == true
end

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
    if contains({"insert", "replace", "upsert", "put"}, operation) then
        tuple = {rnd_key, rnd_value} 
    elseif contains({"select", "get","delete"}, operation) then
        tuple = {rnd_key}
    elseif contains({"update"}, operation) then
        tuple = {rnd_key, operations_list}
    end

    return {operation, tuple}
end

local function get_random_node()
    local index = math.random(#cg.nodes)
    return cg.nodes[index]
end

local function do_random_operation(node, space, max_key)

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

    generated = generate_random_operation(space, max_key, is_follower(conn))
    local operation = generated[1]
    local tuple = generated[2]
    local message = ""

    if operation == "select" then
        
        local success, result = pcall(function()
            return space:select(tuple[1])
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
            space:insert(tuple)
        end)
        if success then
            message = "Запись успешно вставлена: " .. table_to_string(tuple)
        else
            message = "Ошибка при выполнении insert: " .. tostring(err)
        end

    elseif operation == "replace" then
        
        local success, err = pcall(function()
            space:replace(tuple)
        end)
        if success then
            message = "Запись успешно заменена: " .. table_to_string(tuple)
        else
            message = "Ошибка при выполнении replace: " .. tostring(err)
        end

    elseif operation == "update" then
        
        local update_op = {{'=', #tuple, tuple[2]}}  -- mock operation
        local success, err = pcall(function()
            space:update(tuple[1], update_op)
        end)
        if success then
            message = "Запись успешно обновлена " .. table_to_string(tuple)
        else
            message = "Ошибка при выполнении update: " .. tostring(err)
        end

    elseif operation == "upsert" then
        
        local update_op = {{'=', #tuple, tuple[2]}}  -- mock operation
        local success, err = pcall(function()
            space:upsert(tuple, update_op)  
        end)
        if success then
            message = "Запись успешно вставлена или обновлена: " .. table_to_string(tuple)
        else
            message = "Ошибка при выполнении upsert: " .. tostring(err)
        end

    elseif operation == "delete" then
        
        local success, err = pcall(function()
            space:delete(tuple[1])
        end)
        if success then
            message = "Запись успешно удалена для ключа " .. tostring(tuple[1])
        else
            message = "Ошибка при выполнении delete: " .. tostring(err)
        end

    elseif operation == "get" then
        
        local success, result = pcall(function()
            return space:get(tuple[1])
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
            space:replace(tuple)
        end)
        if success then
            message = "Запись " .. table_to_string(tuple) .. " успешно вставлена или обновлена для ключа " .. tostring(tuple[1])
        else
            message = "Ошибка при выполнении put:" .. tostring(err)
        end
    end

    conn:close()
    print(message)
end

---

local function stop_node(node, min_delay, max_delay)

    if min_delay < 0 or max_delay < min_delay then
        error("Некорректные значения min_delay или max_delay")
    end
    local delay = math.random(min_delay, max_delay);

    node:stop()
    print(string.format("Узел: %s остановлен на время: %s", node.alias, delay))
    fiber.sleep(delay)
    print(string.format("Узел: %s запущен вновь", node.alias))
    node:start()
end

---

local function add_delay_to_modifying_operations(node, space_name, min_delay, max_delay)
    
    if min_delay < 0 or max_delay < min_delay then
        error("Некорректные значения min_delay или max_delay")
    end
    local delay = math.random(min_delay, max_delay)

    if not node then
        error(string.format("Узел '%s' не найден", node))
    end

    node:exec(function(space_name, delay)
        local fiber = require('fiber')
        local space = box.space[space_name]
        if not space then
            error(string.format("Пространство '%s' не найдено", space_name))
        end

        local modifiable_operations = { "insert", "replace", "update", "delete", "upsert" }
        
        local function wrap_with_delay(func)
            return function(...)
                fiber.sleep(delay)
                return func(...)
            end
        end

        for _, func_name in ipairs(modifiable_operations) do
            if space[func_name] then
                space[func_name] = wrap_with_delay(space[func_name])
            end
        end

        print(string.format("Задержка добавлена для операций модификации в пространстве '%s' на ноде", space_name))
    end, {space_name, min_delay, max_delay})
end


local function break_connection_between_random_nodes(min_delay, max_delay)

    if min_delay < 0 or max_delay < min_delay then
        error("Некорректные значения min_delay или max_delay")
    end
    local delay = math.random(min_delay, max_delay);

    local node1 = get_random_node()
    local node2 = get_random_node()
    while node1 == node2 do
        node2 = get_random_node()
    end

    node1:exec(function(peer)
        box.cfg{replication = {}}
    end, {node2.net_box_uri})
    print(string.format("Соединение между узлами: %s и %s оборвано на время: %s", node1.alias, node2.alias, delay))

    fiber.sleep(delay) 

    node1:exec(function(peer)
        box.cfg{replication = {peer}}
    end, {node2.net_box_uri})
    print(string.format("Соединение между узлами: %s и %s восстановлено", node1.alias, node2.alias))
end

-- The main test cycle
fiber.create(function()
    while true do
        local random_action = math.random(1, 10)

        if random_action < 10 then
            do_random_operation(get_random_node(), "test", 10)
        else 
            local type_of_crashing = math.random(1, 3)
            if type_of_crashing == 1 then
                stop_node(get_random_node(), 1, 2)
            elseif type_of_crashing == 2 then
                add_delay_to_modifying_operations(get_random_node(), "test", 1, 2)
            else 
                break_connection_between_random_nodes(1, 2)
            end
        end

        fiber.sleep(math.random(1, 2)) 
    end
end)





