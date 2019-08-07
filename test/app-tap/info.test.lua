#!/usr/bin/env tarantool

local tarantool = require('tarantool')

require('tap').test("info", function(test)
    test:plan(9)
    test:like(tarantool.version, '^[1-9]', "version")
    test:isstring(tarantool.package, "package")
    test:ok(_TARANTOOL == tarantool.version, "version")
    test:isstring(tarantool.build.target, "build.target")
    test:isstring(tarantool.build.compiler, "build.compiler")
    test:isstring(tarantool.build.flags, "build.flags")
    test:isstring(tarantool.build.options, "build.options")
    test:ok(tarantool.uptime() > 0, "uptime")
    test:ok(tarantool.pid() > 0, "pid")
end)
