local socket = require('socket')
local sock, err = socket.tcp_connect('127.0.0.1', 3301)
if sock == nil then
    return
end
local s--[[@lsp:object]] = sock
s.--[[@lsp:object_fields]]
