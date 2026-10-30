#!/usr/bin/env tarantool

local tarantool = require('tarantool')
local test = require('tap').test("info")

test:plan(1)

test:test("info", function(test)
    test:plan(11)
    test:like(tarantool.version, '^[1-9]', "version")
    test:isstring(tarantool.package, "package")
    test:ok(_TARANTOOL == tarantool.version, "version")
    test:isstring(tarantool.build.target, "build.target")
    test:isstring(tarantool.build.compiler, "build.compiler")
    test:isstring(tarantool.build.flags, "build.flags")
    test:isstring(tarantool.build.options, "build.options")
    test:ok(tarantool.build.linking == 'static' or
            tarantool.build.linking == 'dynamic',
            "build.linking")
    test:ok(tarantool.uptime() > 0, "uptime")
    test:ok(tarantool.pid() > 0, "pid")

    local tzdata_version = "2022a"
    test:is(tarantool.build.tzdata_version, tzdata_version,
            "build.tzdata_version")
end)

os.exit(test:check() and 0 or 1)
