local fiber = require('fiber')
local fio = require('fio')
local json = require('json')
local popen = require('popen')

local M = {}

-- Return an empty table that is encoded as a JSON object, not an array.
-- Tarantool's json.encode({}) produces [].
function M.map()
    return setmetatable({}, {__serialize = 'map'})
end

function M.source_root()
    local root = os.getenv('TARANTOOL_SRC_DIR')
    if root == nil or root == '' then
        root = fio.cwd()
    end
    return root
end

function M.file_uri(path)
    return 'file://' .. path
end

function M.read_file(path)
    local fh = assert(io.open(path, 'r'))
    local data = fh:read('*a')
    fh:close()
    return data
end

function M.write_file(path, data)
    local fh = assert(fio.open(path,
        {'O_CREAT', 'O_WRONLY', 'O_TRUNC'}, tonumber('0644', 8)))
    fh:write(data)
    fh:close()
end

function M.copy_file(src, dst)
    M.write_file(dst, M.read_file(src))
end

-- Return the byte offset of a --[[@lsp:<name>]] marker.
function M.marker_offset(text, name)
    local needle = '--[[@lsp:' .. name .. ']]'
    local offset = text:find(needle, 1, true)
    if offset == nil then
        error(('marker %s is not found'):format(needle))
    end
    return offset
end

-- Convert a byte offset to an LSP position.
function M.offset_to_position(text, offset)
    local line = 0
    local line_start = 0
    for i = 1, offset - 1 do
        if text:sub(i, i) == '\n' then
            line = line + 1
            line_start = i
        end
    end
    return {line = line, character = offset - line_start - 1}
end

-- Return a position for a marker. delta = -1 targets the token before the
-- marker, which is useful for hover requests.
function M.marker_position(text, name, delta)
    local offset = M.marker_offset(text, name) + (delta or 0)
    return M.offset_to_position(text, offset)
end

local Client = {}
Client.__index = Client

function Client:_send(message)
    local body = json.encode(message)
    local ok, err = self.ph:write(
        'Content-Length: ' .. #body .. '\r\n\r\n' .. body)
    if not ok then
        error('emmylua_ls write failed: ' .. tostring(err))
    end
end

function Client:_read_chunk(timeout)
    local chunk, err = self.ph:read({timeout = timeout})
    if chunk == nil then
        error('emmylua_ls read failed: ' .. tostring(err))
    end
    if chunk == '' then
        error('emmylua_ls exited')
    end
    self.buf = self.buf .. chunk
end

function Client:_read_message(timeout)
    local deadline = fiber.clock() + timeout
    local header_end
    while true do
        header_end = self.buf:find('\r\n\r\n', 1, true)
        if header_end ~= nil then
            break
        end
        self:_read_chunk(math.max(0.1, deadline - fiber.clock()))
    end

    local header = self.buf:sub(1, header_end - 1)
    local length = header:match('[Cc]ontent%-[Ll]ength:%s*(%d+)')
    if length == nil then
        error('emmylua_ls sent an invalid header: ' .. header)
    end
    length = tonumber(length)

    local body_start = header_end + 4
    local body_end = body_start + length - 1
    while #self.buf < body_end do
        self:_read_chunk(math.max(0.1, deadline - fiber.clock()))
    end

    local body = self.buf:sub(body_start, body_end)
    self.buf = self.buf:sub(body_end + 1)
    return json.decode(body)
end

function Client:_handle_server_request(message)
    local method = message.method
    local result = json.NULL
    if method == 'workspace/configuration' then
        local items = message.params and message.params.items or {}
        result = {}
        for _ = 1, #items do
            table.insert(result, json.NULL)
        end
    elseif method == 'workspace/workspaceFolders' then
        result = {{uri = M.file_uri(self.root), name = 'workspace'}}
    end
    self:_send({jsonrpc = '2.0', id = message.id, result = result})
end

function Client:notify(method, params)
    local message = {jsonrpc = '2.0', method = method}
    if params ~= nil then
        message.params = params
    end
    self:_send(message)
end

function Client:request(method, params, timeout)
    self.next_id = self.next_id + 1
    local request_id = self.next_id
    local message = {jsonrpc = '2.0', id = request_id, method = method}
    if params ~= nil then
        message.params = params
    end
    self:_send(message)

    local deadline = fiber.clock() + (timeout or 30)
    while true do
        local response = self:_read_message(
            math.max(0.1, deadline - fiber.clock()))
        if response.method ~= nil and response.id ~= nil then
            self:_handle_server_request(response)
        elseif response.method ~= nil then
            table.insert(self.notifications, response)
        elseif response.id == request_id then
            if response.error ~= nil then
                error(('emmylua_ls error for %s: %s'):format(
                    method, json.encode(response.error)))
            end
            return response.result
        end
    end
end

function Client:request_until(method, params, predicate, timeout)
    local deadline = fiber.clock() + (timeout or 30)
    local last = nil
    while fiber.clock() < deadline do
        local remaining = math.max(0.1, deadline - fiber.clock())
        local ok, result = pcall(
            self.request, self, method, params, math.min(10, remaining))
        if ok then
            last = result
            if predicate(result) then
                return result
            end
        end
        fiber.sleep(0.1)
    end
    error(('timed out waiting for %s; last result: %s'):format(
        method, json.encode(last or json.NULL)))
end

function Client:initialize()
    local root_uri = M.file_uri(self.root)
    local capabilities = {
        workspace = M.map(),
        textDocument = {
            completion = {
                completionItem = {
                    documentationFormat = {'markdown'},
                },
            },
            hover = M.map(),
        },
    }
    local result = self:request('initialize', {
        rootUri = root_uri,
        capabilities = capabilities,
        workspaceFolders = {{uri = root_uri, name = 'workspace'}},
    })
    self:notify('initialized', M.map())
    return result
end

function Client:did_open(path, text)
    self:notify('textDocument/didOpen', {
        textDocument = {
            uri = M.file_uri(path),
            languageId = 'lua',
            version = 1,
            text = text,
        },
    })
end

function Client:close()
    if self.closed then
        return
    end
    self.closed = true
    pcall(function()
        self:request('shutdown', nil, 5)
    end)
    pcall(function()
        self:notify('exit', M.map())
    end)
    pcall(function()
        self.ph:close()
    end)
end

function M.new_client(opts)
    fio.mktree(opts.resources)
    local ph, err = popen.new({
        opts.binary,
        '--communication', 'stdio',
        '--log-path', 'none',
        '--log-level', 'error',
        '--resources-path', opts.resources,
    }, {
        stdin = popen.opts.PIPE,
        stdout = popen.opts.PIPE,
        stderr = popen.opts.DEVNULL,
    })
    if ph == nil then
        error('cannot start emmylua_ls: ' .. tostring(err))
    end
    return setmetatable({
        ph = ph,
        root = opts.workspace,
        buf = '',
        next_id = 0,
        notifications = {},
        closed = false,
    }, Client)
end

return M
