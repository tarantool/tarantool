local msgpack = require('msgpack')
local it--[[@lsp:object]] = msgpack.object({1, 2, 3}):iterator()
it.--[[@lsp:object_fields]]
