local internal = require('xlog.lib')
local fun = require('fun')
local function xlog_pairs(...)
    return fun.wrap(internal.pairs(...))
end

return {
    pairs = xlog_pairs,
}
