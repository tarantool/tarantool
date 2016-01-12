#!/usr/bin/env tarantool

-- #1238 log error if a fiber terminates due to uncaught Lua error

fiber = require('fiber')

box.cfg{logger="pipe:sed -e 's/^.*> //'", log_level=2}

-- must show in the log
fiber.create(loadstring("error('Lua error')","@inline.code"))

-- must NOT show in the log
fiber.create(function() fiber.self():cancel() end)

-- must show in the log
fiber.create(function() box.error(box.error.ILLEGAL_PARAMS, 'oh my') end)

os.exit()
