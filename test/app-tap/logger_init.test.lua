#!/usr/bin/env tarantool

local test = require('tap').test('log')
test:plan(4)

local filename = "1.log"
local message = "Hello, World!"

local log = require('log')
log.init({init_str = filename})

local file = io.open(filename)

while file:read() do
end

log.info(message)
test:is(file:read():match('I>%s+(.*)'), message, 'logging works without box.cfg{}')

log.verbose(message)
test:isnil(file:read(), 'Default log level is 5 (INFO)')

log.level(6)

message = 'updates log level works'
log.verbose(message)
test:is(file:read():match('V>%s+(.*)'), message, 'verbose logging')

box.cfg{
	memtx_memory=107374182
}

while file:read() do
end

message = 'calling box.cfg does not reset logging level'
log.verbose(message)
test:is(file:read():match('V>%s+(.*)'), message, 'verbose logging after box.cfg')

file:close()
test:check()
os.exit()
