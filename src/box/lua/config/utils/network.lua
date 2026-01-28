local urilib = require('uri')

-- Accepts an uri object (one produced by urilib.parse()).
--
-- Performs several checks regarding ability to use the URI to
-- create a client socket. IOW, to call connect() on it.
--
-- The function returns `true` if the URI is OK to connect and
-- `false, err` otherwise.
--
-- If the URI doesn't fit the given criteria an error is raised.
-- The function returns nothing otherwise.
--
-- The following checks are performed:
--
-- * INADDR_ANY IPv4 address (0.0.0.0) or in6addr_any IPv6 address
--   (::) in the host part of the URI.
--
--   It means 'bind to all interfaces' for the bind() call, but it
--   has no meaning at the connect() call on a client.
-- * Zero TCP port (service part of the URI).
--
--   It means 'bind to a random free port' for the bind() call,
--   but it has no meaning at the connect() call on a client.
local function uri_is_suitable_to_connect(uri)
    assert(uri ~= nil)

    if uri.ipv4 == '0.0.0.0' then
        return false, 'INADDR_ANY (0.0.0.0) cannot be used to create ' ..
            'a client socket'
    end
    if uri.ipv6 == '::' then
        return false, 'in6addr_any (::) cannot be used to create a client ' ..
            'socket'
    end
    if uri.service == '0' then
        return false, 'An URI with zero port cannot be used to create ' ..
            'a client socket'
    end

    return true
end

local function parse_listen(listen)
    if type(listen) ~= 'string' and type(listen) ~= 'number' then
        return nil, nil, 'listen must be a string or a number, got ' ..
            type(listen)
    end

    local host
    local port
    if type(listen) == 'string' then
        local uri, err = urilib.parse(listen)
        if err ~= nil then
            return nil, nil, 'failed to parse URI: ' .. err
        end

        if uri.scheme ~= nil then
            if uri.scheme == 'unix' then
                uri.unix = uri.path
            else
                return nil, nil, 'URI scheme is not supported'
            end
        end

        if uri.login ~= nil or uri.password ~= nil then
            return nil, nil, 'URI login and password are not supported'
        end

        if uri.query ~= nil then
            return nil, nil, 'URI query component is not supported'
        end

        if uri.unix ~= nil then
            host = 'unix/'
            port = uri.unix
        else
            if uri.service == nil then
                return nil, nil, 'URI must contain a port'
            end

            port = tonumber(uri.service)
            if port == nil then
                return nil, nil, 'URI port must be a number'
            end
            if uri.host ~= nil then
                host = uri.host
            elseif uri.ipv4 ~= nil then
                host = uri.ipv4
            elseif uri.ipv6 ~= nil then
                host = uri.ipv6
            else
                host = '0.0.0.0'
            end
        end
    elseif type(listen) == 'number' then
        host = '0.0.0.0'
        port = listen
    end

    if type(port) == 'number' and (port < 1 or port > 65535) then
        return nil, nil, 'port must be in the range [1, 65535]'
    end

    return host, port, nil
end

return {
    parse_listen = parse_listen,
    uri_is_suitable_to_connect = uri_is_suitable_to_connect,
}
