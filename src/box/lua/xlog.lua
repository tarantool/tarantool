local internal = require('xlog')
local fun = require('fun')
local function xlog_pairs(...)
    return fun.wrap(internal.pairs(...))
end

package.loaded['xlog'] = {
    pairs = xlog_pairs,
}
