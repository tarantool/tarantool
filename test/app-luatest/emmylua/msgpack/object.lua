local msgpack = require('msgpack')
local obj--[[@lsp:object]] = msgpack.object({1, 2, 3})
obj.--[[@lsp:object_fields]]
