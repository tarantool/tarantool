local xlog = require('xlog')
local _, entry = xlog.pairs('00000000000000000000.xlog')()
local header--[[@lsp:object]] = entry.HEADER
header.--[[@lsp:object_fields]]
