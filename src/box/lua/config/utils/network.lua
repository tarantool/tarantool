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

return {
    uri_is_suitable_to_connect = uri_is_suitable_to_connect,
}
