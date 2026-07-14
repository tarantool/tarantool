local builtin = debug.getregistry()._TARANTOOL_BUILTIN
for _, name in ipairs({'http.lib', 'http.codes', 'http.mime_types',
                       'http.sslsocket', 'http.version'}) do
    builtin['internal.' .. name] = builtin[name]
    builtin[name] = nil
    package.loaded[name] = nil
end

local loaders = require('internal.loaders')

-- The bundled roles capture the module on load. The proxy makes them use
-- an installed rock if there is one.
package.loaded['http.server'] = {
    new = function(...)
        local http_server = loaders.require_first('http.server',
                                                  'internal.http.server')
        return http_server.new(...)
    end,
}
