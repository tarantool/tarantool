local t = require('luatest')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fiber = require('fiber')
local net_box = require('net.box')

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

-- Initial setup
for _, node in ipairs(cg.nodes) do
    local node_state = node:exec(function()
        return box.info.election.state
    end)
    print(string.format("Node %s is %s", node.alias, tostring(node_state)))
end

-- We determine which node is the leader
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

--Creating a space on the leader
local result = leader_node:exec(function()
    local log_message = ""

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
        log_message = "Space 'test' is created and configured on the leader"
    else
        log_message = "Space 'test' is created and configured on the leader"
    end
    return log_message
end)

print(result)

local function contains(tbl, value)
    for _, v in ipairs(tbl) do
        if v == value then
            return true
        end
    end
    return false
end

local function is_follower(conn)
    return conn:call('box.info').ro == true
end

-- todo: операции с транзакциями
-- todo: учет параметров спейса
local function generate_random_operation(space, max_key, is_read_only)

    local operations
    if is_read_only then
        operations = {"select", "get"}
    else
        operations = {"select", "insert", "replace", "update", "upsert", "delete", "get", "put"}
    end
    local operation = operations[math.random(#operations)]

    -- todo: причесать
    local rnd_key = math.random(1, max_key) -- Случайный ключ
    local rnd_value = "Value_" .. rnd_key   -- Случайное значение

    local tuple
    if contains({"insert", "replace", "upsert", "put"}, operation) then
        tuple = {rnd_key, rnd_value} 
    elseif contains({"select", "get","delete"}, operation) then
        tuple = {rnd_key}
    elseif contains({"update"}, operation) then
        tuple = {rnd_key, operations_list}
    end

    if operation == "select" then
        
        local success, result = pcall(function()
            return space:select({rnd_key})
        end)
        if success then
            if #result == 0 then
                print("Записи не найдены для ключа " .. rnd_key)
            else
                print("Результаты Select для ключа " .. rnd_key .. ":")
                for _, tuple in ipairs(result) do
                    print(tuple)
                end
            end
        else
            print("Ошибка при выполнении select:", result)
        end

    elseif operation == "insert" then
        
        local success, err = pcall(function()
            space:insert(tuple)
        end)
        if success then
            print("Запись успешно вставлена:", tuple)
        else
            print("Ошибка при вставке записи:", err)
        end

    elseif operation == "replace" then
        
        local success, err = pcall(function()
            space:replace(tuple)
        end)
        if success then
            print("Запись успешно заменена:", tuple)
        else
            print("Ошибка при замене записи:", err)
        end

    elseif operation == "update" then
        
        local update_op = {{'=', #tuple, rnd_value}}  
        local success, err = pcall(function()
            space:update(rnd_key, update_op)
        end)
        if success then
            print("Запись успешно обновлена для ключа " .. rnd_key)
        else
            print("Ошибка при обновлении записи:", err)
        end

    elseif operation == "upsert" then
        
        local update_op = {{'=', #tuple, rnd_value}}  
        local success, err = pcall(function()
            space:upsert(tuple, update_op)  
        end)
        if success then
            print("Запись успешно вставлена или обновлена для ключа " .. rnd_key)
        else
            print("Ошибка при upsert:", err)
        end

    elseif operation == "delete" then
        
        local success, err = pcall(function()
            space:delete(rnd_key)
        end)
        if success then
            print("Запись успешно удалена для ключа " .. rnd_key)
        else
            print("Ошибка при удалении записи:", err)
        end

    elseif operation == "get" then
        
        local success, result = pcall(function()
            return space:get(rnd_key)
        end)
        if success then
            if result then
                print("Результат Get для ключа " .. rnd_key .. ":", result)
            else
                print("Запись не найдена для ключа " .. rnd_key)
            end
        else
            print("Ошибка при выполнении get:", result)
        end

    elseif operation == "put" then
        
        local success, err = pcall(function()
            space:replace(tuple)
        end)
        if success then
            print("Запись успешно вставлена или обновлена для ключа " .. rnd_key)
        else
            print("Ошибка при put:", err)
        end
    end
end

local function get_random_node()
    local index = math.random(#cg.nodes)
    return cg.nodes[index]
end

local function execute_on_random_node(space, max_key)
    local node = get_random_node()
    local uri = node.net_box_uri
    print(string.format("Подключение к ноде: %s", uri))
    
    local conn = require('net.box').connect(uri)
    if not conn then
        print("Ошибка подключения к ноде:", uri)
        return
    end

    local space = conn.space.test  -- Используем имя нашего пространства
    if not space then
        print("Ошибка: Space 'test' не найден на ноде:", uri)
        conn:close()
        return
    end

    generate_random_operation(space, max_key, is_follower(conn))

    conn:close()
end


fiber.create(function()
    while true do
        execute_on_random_node(space, 100)
        fiber.sleep(math.random(0.1, 1)) -- Интервал между операциями
    end
end)

