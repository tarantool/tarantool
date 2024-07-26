local t = require('luatest')
local uri = require('uri')

-- IPv6 addresses from RFC 2732 corresponding to IPv6 addresses
-- processed by uri (refer to src/lib/uri/uri_parser.rl:114).
local addrs = {
    '[FEDC:BA98:7654:3210:FEDC:BA98:7654:3210]',
    '[1080:0:0:0:8:800:200C:4171]',
    '[3ffe:2a00:100:7031::1]',
    '[1080::8:800:200C:417A]',
    '[::FFFF:129.144.52.38]',
    '[2010:836b:4179::836b:4179]'
}

local g1 = t.group('group_parse_ipv6', t.helpers.matrix({
    schema = {nil, 'http'},
    service = {nil, 80},
    path = {nil, 'foo', 'index.html'},
    u = addrs,
}))

local function join_url(params)
    local url = ''
    if params.schema ~= nil then
        url = params.schema .. '://'
    end
    url = url .. params.u
    if params.service ~= nil then
        url = url .. ':' .. params.service
    end
    if params.path ~= nil then
        url = url .. '/' .. params.path
    end
    return url
end

g1.test_parse_urls_with_ipv6_addresses = function(cg)
    -- This test verifies the correct handling and parsing of URIs containing
    -- IPv6 addresses as specified in RFC 2732.
    local url = join_url(cg.params)
    local res = uri.parse(url)
    t.assert_not_equals(res, nil, string.format('Error while parsing %q', url))
    t.assert_equals(type(res), 'table',
                    string.format('uri.parse(%q) is not a table', url))
    local expected = {
        schema = cg.params.schema,
        host = cg.params.u,
        service = cg.params.service,
        path = cg.params.path,
        ipv6 = cg.params.u,
    }
    for key, value in ipairs(expected) do
        local msg = string.format('Mismatch field %q: expected %q, got %q',
                                    key, tostring(res[key]), tostring(value))
        t.assert_equals(value, res[key], msg)
    end
end
