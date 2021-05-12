#!/usr/bin/env tarantool

local ffi = require('ffi')
local fio = require('fio')
local net = { box = require('net.box') }

--box.cfg{log = "tarantool.log"}
-- Use BUILDDIR passed from test-run or cwd when run w/o
-- test-run to find test/app-tap/session_module_api.{so,dylib}.
local build_path = os.getenv("BUILDDIR") or '.'
package.cpath = fio.pathjoin(build_path, 'test/app-tap/?.so'   ) .. ';' ..
                fio.pathjoin(build_path, 'test/app-tap/?.dylib') .. ';' ..
                package.cpath

box.cfg{
    listen = os.getenv('LISTEN');
    log="tarantool.log";
}

local uri = require('uri').parse(box.cfg.listen)
local HOST, PORT = uri.host or 'localhost', uri.service
session = box.session
space = box.schema.space.create('tweedledum')
space:create_index('primary', { type = 'hash' })

require('tap').test("session_module_api", function(test)
    test:plan(4)
    local status, module = pcall(require, 'module_api')
    test:is(status, true, "module")
    test:ok(status, "module is loaded")
    if not status then
        test:diag("Failed to load library:")
        for _, line in ipairs(module:split("\n")) do
            test:diag("%s", line)
        end
        return
    end

    local space  = box.schema.space.create("test")
    space:create_index('primary')

    local session_id = 1
    local function get_session_id() session_id = box.session.id() end
    session.on_connect(get_session_id)

    test:ok(module.session_check_default(), "session_check_default is ok")

    local c = net.box.connect(HOST, PORT)
    test:ok(module.session_check_peer_exists(session_id), "session_check_peer_exists is ok")
end)

os.exit(0)
