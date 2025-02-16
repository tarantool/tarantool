local net_box = require('net.box')

local function is_follower(conn)
    return conn:call('box.info').ro == true
end

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

-- Calculating the delay
local function calculate_delay(min_delay, max_delay)

    if min_delay < 0 or max_delay < min_delay then
        error(string.format(
            "Invalid delay values: min_delay (%d) should be >= 0 and <= max_delay (%d)", 
            min_delay, max_delay
        ))
    end

    local delay = math.random(min_delay, max_delay);
    return delay
end

local function check_node(node)
    if not node then
        error("The node is not specified")
    end
end

local function get_initial_replication(nodes)
    local initial_replication = {}
    for _, node in ipairs(nodes) do
        local replication = node:exec(function()
            return box.cfg.replication
        end)
        initial_replication[node.alias] = replication
    end
    return initial_replication
end

local function get_random_node(nodes, timeout)
    if not nodes or #nodes == 0 then
        error("Node list is empty or nil")
    end

    for _, node in ipairs(nodes) do
        local ok, result = pcall(function()
            return node:eval("return true", {}, {timeout = timeout})
        end)

        if ok and result then
            return node
        end
    end

    error("No connected nodes available")
end


return {
    print_hello = print_hello,
    contains = contains,
    is_follower = is_follower,
    table_to_string = table_to_string,
    calculate_delay = calculate_delay,
    check_node = check_node,
    get_initial_replication = get_initial_replication,
    get_random_node = get_random_node,

}