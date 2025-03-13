local socket = require("socket")
local json = require("dkjson")

-- Simulating some cluster data
local cluster = {
    nodes = {},
    logs = {},
    has_error = 0
}

local is_created = 0

-- Create a cluster with the specified count of nodes
local function create_cluster(count)
    cluster.nodes = {}
    for i = 1, count do
        table.insert(cluster.nodes, {id = i})
    end
    table.insert(cluster.logs, "Cluster created with " .. count .. " nodes")
end

-- Apply an operation (simulation of applying a crash or change)
local function apply_operation(op)
    -- Simulate applying the operation
    table.insert(cluster.logs, "Operation applied: " .. json.encode(op))
end

-- Revert an operation (simulation of reverting a crash or change)
local function revert_operation(op)
    -- Simulate reverting the operation
    table.insert(cluster.logs, "Operation reverted: " .. json.encode(op))
end

local function handle_start(req)
    local status = 200
    local result = { created = is_created }
    local data = req:json()

    if not data then
        status = 400
        result = { error = "Invalid JSON" }
    elseif data.count then
        create_cluster(data.count)
        is_created = 1
        result.created = is_created
    else
        status = 400
        result = { error = "Invalid JSON format or missing 'count'" }
    end

    return req:render({status = status, body = json.encode(result)})
end

-- Обработчик запроса /simulate
local function handle_simulate(req)
    local status = 200
    local result = {
        nodes_cnt = #cluster.nodes,
        logs = table.concat(cluster.logs, "\n"),
        has_error = cluster.has_error
    }

    local data = req:json()

    if not data then
        cluster.has_error = 1
        table.insert(cluster.logs, "JSON parse error")
        status = 400
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
                table.insert(cluster.logs, "Operation scheduled: " .. json.encode(op))
            else
                cluster.has_error = 1
                table.insert(cluster.logs, "Invalid operation: " .. error_msg)
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
if server == nil then
    print("Failed to bind to port 9090")
    return
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
                local post_data = read_post_data(client)
                local req = {
                    json = function()
                        return json.decode(post_data)
                    end,
                    render = function(self, response)
                        client:send("HTTP/1.1 " .. response.status .. " OK\r\n")
                        client:send("Content-Type: application/json\r\n")
                        client:send("\r\n")
                        client:send(response.body)
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
