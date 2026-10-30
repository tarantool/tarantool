local internal = require('xlog.lib')
local fun = require('fun')
local function xlog_pairs(...)
    return fun.wrap(internal.pairs(...))
end

local function xlog_meta(...)
    return internal.meta(...)
end

return {
    pairs = xlog_pairs,
    meta = xlog_meta,
}
