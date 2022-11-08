local dbg = require 'luadebug'
dbg()
local date = require 'datetime'

local T = date.new{hour = 3, tzoffset = '+0300'}
print(T)

os.exit(0)
