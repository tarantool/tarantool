local config = require('config')
local checks = require('checks')
local netbox = require('net.box')

local connections = {}

local function is_connection_valid(conn, opts)
    if conn == nil or conn.state == 'error' or conn.state == 'closed' then
        return false
    end
    assert(type(opts) == 'table')
    local conn_opts = conn.opts or {}
    if opts.fetch_schema ~= false and conn_opts.fetch_schema == false then
        return false
    end
    return true
end

local function connect(instance_name, opts)
    checks('string', {
        connect_timeout = '?number',
        wait_connected = '?boolean',
        fetch_schema = '?boolean',
    })
    opts = opts or {}

    local conn = connections[instance_name]
    if not is_connection_valid(conn, opts) then
        local uri = config:instance_uri('peer', {instance = instance_name})
        if uri == nil then
            local err = 'No suitable URI provided for instance %q'
            error(err:format(instance_name), 0)
        end

        local conn_opts = {
            connect_timeout = opts.connect_timeout,
            wait_connected = false,
            fetch_schema = opts.fetch_schema,
        }
        local ok, res = pcall(netbox.connect, uri, conn_opts)
        if not ok then
            local msg = 'Unable to connect to instance %q: %s'
            error(msg:format(instance_name, res.message), 0)
        end
        conn = res
        connections[instance_name] = conn
    end

    -- If opts.wait_connected is not false we wait until the connection is
    -- established or an error occurs (including a timeout error).
    if opts.wait_connected ~= false and conn:wait_connected() == false then
        local msg = 'Unable to connect to instance %q: connection timeout'
        error(msg:format(instance_name), 0)
    end
    return conn
end

return {
    connect = connect,
}
