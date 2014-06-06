#!/usr/bin/env tarantool

--
-- Check that Tarantool creates ADMIN session for #! script
-- 
local filename = "1.log"
local message = "Hello, World!"
box.cfg{logger=filename}
local log = require('log')
local io = require('io')
local file = io.open(filename)
while file:read() do
end
log.info(message)
local line = file:read()
print(line:sub(-message:len()))
file:close()
log.rotate()
os.exit()
