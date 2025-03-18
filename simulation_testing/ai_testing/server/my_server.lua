package.path = package.path .. ";../../src/?.lua"

local socket = require("socket")
local json = require("dkjson")
local cluster = require('random_cluster')
local logger = require("logger")
local crash_functions = require("crash_functions")
local tools = require("tools")
local fiber = require("fiber")
math.randomseed(os.time())

local cluster_condition = {
    nodes_count = 0,
    logs = {},
    has_error = 0
}

function extra_logs(msg)
    table.insert(cluster_condition.logs, msg)
end

logger.init_logger()

local CLUSTER = {}
local initial_replication = {}

local function init_cluster(data)
    print("Initializing cluster with count:", data.count)
    CLUSTER = cluster.make_cluster(data.count)
    cluster_condition.nodes_count = data.count
    initial_replication = tools.get_initial_replication(CLUSTER.replicas)
    extra_logs("Cluster initialized with " .. data.count .. " nodes")
end

local function apply_operation(op)
    local crash_nodes = {}
    table.insert(crash_nodes, op.node_1)
    if op.node_2 ~= -1 then
        table.insert(crash_nodes, op.node_2)
    end
    crash_functions.crash_simulation(
        CLUSTER,
        nodes_activity_states,
        initial_replication,
        op.crash_type,
        op.crash_time,
        crash_nodes,
        {}
    )
end

local function handle_start(req)
    local status = 200
    local result = { created = 0 }
    local data = req:json()

    if not data then
        status = 400
        result = { error = "Invalid JSON" }
        extra_logs("Invalid JSON in start request")
    elseif data.count then
        local ok, err = pcall(init_cluster, data)
        if ok then
            result = { created = 1 }
            extra_logs("Cluster start successful")
        else
            status = 400
            result = { created = 0, error = tostring(err) }
            extra_logs("Cluster start failed: " .. tostring(err))
        end
    else
        status = 400
        result = { created = 0, error = "Missing count parameter" }
        extra_logs("Missing count parameter in start request")
    end

    req:render({
        status = status,
        body = json.encode(result)
    })
end

local function handle_simulate(req)
    local status = 200
    local result = {
        nodes_count = cluster_condition.nodes_count,
        logs = table.concat(cluster_condition.logs, "\n"),
        has_error = cluster_condition.has_error
    }

    local data = req:json()

    if not data then
        cluster_condition.has_error = 1
        extra_logs("JSON parse error in simulate request")
        status = 400
        result = { error = "Invalid JSON" }
    elseif data.operations then
        for _, op in ipairs(data.operations) do
            local valid = true
            local error_msg = ""

            -- Validate operation
            if not op.crash_type or (op.crash_type < 0 or op.crash_type > 2) then
                valid = false
                error_msg = "Invalid crash_type: " .. tostring(op.crash_type)
            elseif op.crash_type == 2 then
                if not op.node_1 or not op.node_2 or op.node_1 == op.node_2 then
                    valid = false
                    error_msg = "Invalid nodes for crash_type 2"
                end
            else
                if op.node_2 ~= -1 then
                    valid = false
                    error_msg = "node_2 must be -1 for this crash_type"
                end
            end

            if valid then
                extra_logs("Applying operation: " .. json.encode(op))
                local ok, err = pcall(apply_operation, op)
                if not ok then
                    cluster_condition.has_error = 1
                    extra_logs("Operation failed: " .. tostring(err))
                end
            else
                cluster_condition.has_error = 1
                extra_logs("Invalid operation: " .. error_msg)
            end
        end
    end

    -- Reset logs after each simulation step
    cluster_condition.logs = {}
    req:render({
        status = status,
        body = json.encode(result)
    })
end

local function parse_request(request)
    local method, path = request:match("^(%a+)%s+([%S]+)")
    return method, path
end

local function read_post_data(client)
    local body = ""
    local content_length = 0
    
    -- Read headers to find Content-Length
    while true do
        local line, err = client:receive("*l")
        if err or line == "" then break end
        
        local cl = line:lower():match("content%-length:%s*(%d+)")
        if cl then
            content_length = tonumber(cl)
        end
    end

    -- Read body if content_length > 0
    if content_length > 0 then
        body = client:receive(content_length)
    end

    return body
end

local function handle_request(client)
    local request, err = client:receive("*l")
    if not err and request then
        local method, path = parse_request(request)
        print("Received", method, "request for", path)

        if method == "POST" then
            local post_data = read_post_data(client)
            local req = {
                json = function() return json.decode(post_data) end,
                render = function(response)
                    local body = response.body
                    client:send("HTTP/1.1 " .. response.status .. " OK\r\n")
                    client:send("Content-Type: application/json\r\n")
                    client:send("Content-Length: " .. #body .. "\r\n\r\n")
                    client:send(body)
                end
            }

            if path == "/start" then
                local ok, _  = pcall(handle_start, req)
                if not ok then
                    client:send("HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n")
                else

                end
            elseif path == "/simulate" then
                local ok, _ = pcall(handle_simulate, req)
                if not ok then
                    client:send("HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n")
                end
            else
                client:send("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n")
            end
        else
            client:send("HTTP/1.1 405 Method Not Allowed\r\nContent-Length: 0\r\n\r\n")
        end
    else
        client:send("HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n")
    end
    client:close()
end

-- Server initialization
local server = socket.bind("0.0.0.0", 9090)
while not server do
    fiber.sleep(1)
    server = socket.bind("0.0.0.0", 9090)
end
print("Server running on port 9090")

while true do
    local client = server:accept()
    if client then
        fiber.create(handle_request, client)
    end
end

-- package.path = package.path .. "../../src/?.lua"

-- local socket = require("socket")
-- local json = require("dkjson")
-- local cluster = require('random_cluster')
-- local logger = require("logger")
-- local crash_functions = require("crash_functions")
-- local tools = require("tools")
-- local fiber = require("fiber")
-- math.randomseed(os.time())

-- local cluster_condition = {
--     nodes_count = 0,
--     logs = {},
--     has_error = 0
-- }

-- function extra_logs(msg)
--     table.insert(cluster_condition.logs, msg)
-- end

-- logger.init_logger()



-- local CLUSTER = {}
-- local initial_replication = {}

-- local function init_cluster(data)
--     print("count == ", tonumber(data.count))
--     CLUSTER = cluster.make_cluster( tonumber(data.count))
--     print("XERS_0")
--     initial_replication = tools.get_initial_replication(CLUSTER.replicas)
-- end


-- local function apply_operation(op)
--     local crash_nodes = {}
--     table.insert(crash_nodes, op.node_1)
--     if op.node_2 ~= -1 then
--         table.insert(crash_nodes, op.node_2)
--     end
--     crash_functions.crash_simulation(
--         CLUSTER,
--         nodes_activity_states,
--         initial_replication,
--         op.crash_type,
--         op.crash_time,
--         crash_nodes,
--         {}
--     )
-- end


-- local function handle_start(req)
--     local status = 200
--     local result = { created = 0 }
--     local data = req:json()

--     if not data then
--         status = 400
--         result = { error = "Invalid JSON" }
--     elseif data.count then
--         local ok = pcall(init_cluster, data)
--         if ok then
--             result = { created = 1 }
--         else
--             status = 400
--             result = { created = 0 }
--         end
--         print("result == ",  json.encode(result))
--     else
--         status = 400
--         result = { created = 0 }
--     end
--     local response_body = json.encode(result)
--     client:send("HTTP/1.1 " .. status .. " OK\r\n")
--     client:send("Content-Type: application/json\r\n")
--     client:send("Content-Length: " .. #response_body .. "\r\n\r\n")
--     client:send(response_body)

-- end

-- -- Обработчик запроса /simulate
-- local function handle_simulate(req)
--     local status = 200
--     local result = {
--         nodes_cnt = #cluster_condition.nodes,
--         logs = table.concat(cluster_condition.logs, "\n"),
--         has_error = cluster_condition.has_error
--     }

--     local data = req:json()

--     if not data then
--         cluster_condition.has_error = 1
--         table.insert(cluster_condition.logs, "JSON parse error")
--         status = 400
--         result = { error = "Invalid JSON" }
--     elseif data.operations then
--         for _, op in ipairs(data.operations) do
--             local valid = true
--             local error_msg = ""

--             -- Validate the operation
--             if not op.crash_type or (op.crash_type < 0 or op.crash_type > 2) then
--                 valid = false
--                 -- error_msg = "Invalid crash_type"
--             elseif op.crash_type == 2 then
--                 if not op.node_1 or not op.node_2 or op.node_1 == op.node_2 then
--                     valid = false
--                     -- error_msg = "Invalid nodes for crash_type 2"
--                 end
--             else
--                 if op.node_2 ~= -1 then
--                     valid = false
--                     -- error_msg = "node_2 must be -1 for this crash_type"
--                 end
--             end

--             if valid then
--                 -- Simulate applying and reverting operation using fiber (simulated here)
--                 apply_operation(op)
--             else
--                 cluster_condition.has_error = 1
--             end
--         end
--     end

--     req:render({status = status, body = json.encode(result)})
-- end

-- -- Function to parse the HTTP request method and path
-- local function parse_request(request)
--     local method, path = request:match("^(%a+)%s([%S]+)")
--     return method, path
-- end

-- local function read_post_data(client)
--     local body = ""
--     local headers = {}

--     -- Read headers
--     while true do
--         local line, err = client:receive("*l")
--         if err then
--             print("Error reading line:", err)
--             break
--         else
--             if line == "" then break end
--             table.insert(headers, line)
--         end
--     end

--     -- Look for Content-Length header and read the post data
--     for _, header in ipairs(headers) do
--         if header:lower():match("content%-length") then
--             local content_length = tonumber(header:match("Content%-Length:%s*(%d+)"))
--             if content_length then
--                 body = client:receive(content_length)
--             end
--         end
--     end

--     -- Log body to ensure it's correctly read
--     print("Received POST data: " .. (body or "<empty>"))

--     return body
-- end



-- local function handle_request(client)
--     -- client:settimeout(1)
--     -- Read the HTTP request line
--     local request, err = client:receive("*l")
--     if not err then
--         print("Request received: " .. request)

--         -- Parse the method and path
--         local method, path = parse_request(request)

--         -- Handle POST requests for /start and /simulate
--         if method == "POST" then
--             local post_data = read_post_data(client)
--             local req = {
--                 json = function()
--                     return json.decode(post_data)
--                 end,
--                 render = function(response)
--                     client:send("HTTP/1.1 " .. response.status .. " OK\r\n")
--                     print("sent HTTP/1.1 " .. response.status .. " OK\r\n")
--                     client:send("Content-Type: application/json\r\n")
--                     client:send("\r\n")
--                     print( "sent Content-Type: application/json\r\n")
--                     client:send(response.body)
--                     print("sent body: " .. response.body)
--                 end
--             }

--             if path == "/start" then
--                 local ok = pcall(handle_start, req)
--                 if not ok then
--                     client:send("HTTP/1.1 500 Internal Server Error\r\n")
--                     client:send("Content-Type: text/plain\r\n")
--                     client:send("\r\n")
--                     client:send("error handling start request\r\n")
--                 end
--             elseif path == "/simulate" then
--                 local ok = pcall(handle_simulate, req)
--                 if not ok then
--                     client:send("HTTP/1.1 500 Internal Server Error\r\n")
--                     client:send("Content-Type: text/plain\r\n")
--                     client:send("\r\n")
--                     client:send("error handling simulating request\r\n")
--                 end
--             else
--                 client:send("HTTP/1.1 404 Not Found\r\n")
--                 client:send("Content-Type: text/plain\r\n")
--                 client:send("\r\n")
--                 client:send("Not Found\r\n")
--             end
--         else
--             client:send("HTTP/1.1 405 Method Not Allowed\r\n")
--             client:send("Content-Type: text/plain\r\n")
--             client:send("\r\n")
--             client:send("Method Not Allowed\r\n")
--         end
--     end

--     client:close()
-- end

-- local server = socket.bind("0.0.0.0", 9090)
-- while server == nil do
--     server = socket.bind("0.0.0.0", 9090)
--     print("Failed to bind to port 9090")
--     fiber.sleep(1)
-- end
-- -- server:settimeout(1)
-- print("Server running on http://localhost:9090/")

-- while true do
--     local client = server:accept()
--     if client then
--         fiber.create(handle_request, client)
--     end
-- end
