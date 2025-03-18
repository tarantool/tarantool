package.path = package.path .. ";../../src/?.lua"

local socket = require("socket")
local json = require("dkjson")
local cluster = require('random_cluster')
local logger = require("logger")
local crash_functions = require("crash_functions")
local tools = require("tools")
local fiber = require("fiber")

-- Simulating some cluster data
state = {
    nodes_cnt = 0,
    logs = "",
    has_error = 0
}

function extra_logs(msg)
    state.logs = state.logs .. "\n" .. msg
end

logger.init_logger()

CLUSTER = {}
local initial_replication = {}

local is_created = 0

-- Create a cluster with the specified count of nodes
local function create_cluster(count)
    return fiber.create(function()
    state.nodes_count = count
    CLUSTER = cluster.make_cluster(count)
    fiber.sleep(20)
    initial_replication = tools.get_initial_replication(CLUSTER)
    end)
end


local function apply_operation(op)
    fiber.create(function() 
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
    end)
end

-- -- Apply an operation (simulation of applying a crash or change)
-- local function apply_operation(op)
--     -- Simulate applying the operation
--     table.insert(state.logs, "Operation applied: " .. json.encode(op))
-- end



local function handle_start(req)
    local status = "200"
    local result = { created = is_created }
    local data = req:json()

    if not data then
        status = "400"
        result = { error = "Invalid JSON" }
    elseif data.count then
        local ok, err = pcall(function() create_cluster(data.count) end)
        fiber.sleep(20)
        if not ok then
            status = "400"
            result = { error = err }
        end
        if ok then
            is_created = 1
            result.created = is_created
        end
    else
        status = "400"
        result = { error = "Invalid JSON format or missing 'count'" }
    end

    req:render({status = status, body = json.encode(result)})
end

-- Обработчик запроса /simulate
local function handle_simulate(req)
    local status = "200"
    local result = {
        nodes_count = state.nodes_count,
        logs = state.logs,
        has_error = state.has_error
    }

    local data = req:json()

    if not data then
        state.has_error = 1
        table.insert(state.logs, "JSON parse error")
        status = "400"
        result = { error = "Invalid JSON" }
    elseif data.operations then
        for _, op in ipairs(data.operations) do
            local valid = true
            local error_msg = ""

            -- Validate the operation
            if not op.crash_type or (op.crash_type < 0 or op.crash_type > 2) then
                valid = false
                error_msg = "Invalid crash_type"
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
                -- Simulate applying and reverting operation using fiber (simulated here)
                apply_operation(op)
            else
                state.has_error = 1
                status = "400"
                table.insert(state.logs, "Invalid operation: " .. error_msg)
            end
        end
    end

    return req:render({status = status, body = json.encode(result)})
end

-- Function to parse the HTTP request method and path
local function parse_request(request)
    local method, path = request:match("^(%a+)%s([%S]+)")
    return method, path
end

local function read_post_data(client)
    local body = ""
    local headers = {}

    -- Read headers
    while true do
        local line, err = client:receive("*l")
        if not err then
            if line == "" then break end
            table.insert(headers, line)
        else
            break
        end
    end

    -- Look for Content-Length header and read the post data
    for _, header in ipairs(headers) do
        if header:lower():match("content%-length") then
            local content_length = tonumber(header:match("Content%-Length:%s*(%d+)"))
            if content_length then
                body = client:receive(content_length)
            end
        end
    end

    -- Log body to ensure it's correctly read
    print("Received POST data: " .. (body or "<empty>"))

    return body
end


-- Main HTTP server loop
local server = socket.bind("*", 9090)
while server == nil do
    print("Failed to bind to port 9090")
    server = socket.bind("*", 9090)
    fiber.sleep(1)
end
server:settimeout(0)
print("Server running on http://localhost:9090/")

while true do
    local client = server:accept()
    if client then
        client:settimeout(10)

        -- Read the HTTP request line
        local request, err = client:receive("*l")
        if not err then
            print("Request received: " .. request)

            -- Parse the method and path
            local method, path = parse_request(request)

            -- Handle POST requests for /start and /simulate
            if method == "POST" then
                local req = {
                    json = function()
                        return json.decode(read_post_data(client))
                    end,
                    render = function(self,response)
                        client:send("HTTP/1.1 " .. tostring(response.status) .. " OK\r\nContent-Type: application/json\r\n\r\n" .. response.body.."\r\n")
                    end
                }

                if path == "/start" then
                    handle_start(req)
                elseif path == "/simulate" then
                    handle_simulate(req)
                else
                    client:send("HTTP/1.1 404 Not Found\r\n")
                    client:send("Content-Type: text/plain\r\n")
                    client:send("\r\n")
                    client:send("Not Found\r\n")
                end
            else
                client:send("HTTP/1.1 405 Method Not Allowed\r\n")
                client:send("Content-Type: text/plain\r\n")
                client:send("\r\n")
                client:send("Method Not Allowed\r\n")
            end
        end

        client:close()
    end
end