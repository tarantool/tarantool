local date = require 'datetime'

local T = date.new{hour = 3, tzoffset = '+0300'}
print(T)

local fmt = '%Y-%m-%dT%H%M%z'
local S  = T:format(fmt)
print(S)
local T1 = date.parse(S, {format = fmt})
print(T1)

os.exit(0)
