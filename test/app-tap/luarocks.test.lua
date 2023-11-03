#!/usr/bin/env tarantool

local tap = require('tap')
local tarantool = require('tarantool')

local test = tap.test('embedded-luarocks')

if tarantool.build.linking == 'dynamic' then
    test:plan(1)
    test:skip('luarocks is not embedded in dynamic build')
    os.exit(0)
end

test:plan(3)

local function find_luarocks_modules(tbl)
    local ret = {}
    for k, _ in pairs(tbl) do
        if k:find('luarocks') ~= nil then
            table.insert(ret, k)
        end
    end
    return ret
end

local function diag_msg(prefix, tbl)
    local fmt = '%s (%d count)' .. (#tbl > 0 and ':\n - %s' or '%s')
    return string.format(fmt, prefix, #tbl, table.concat(tbl, '\n - '))
end

-- Check that luarocks not loaded at startup
local loaded_luarocks = find_luarocks_modules(package.loaded)
test:ok(#loaded_luarocks == 0, 'lauroks not loaded at startup')
test:diag(diag_msg('Loaded luarocks modules at startup', loaded_luarocks))

-- Check that all luarocks modules exists at package.preload
local preload_luarocks = find_luarocks_modules(package.preload)
test:ok(#preload_luarocks == 91, 'preloaded rocks count equals')
test:diag(diag_msg('Preloaded luarocks module', preload_luarocks))

-- Check that luarocks.core.cfg works
local cfg = require("luarocks.core.cfg")
cfg.init()
local loaded_luarocks_cfg = find_luarocks_modules(package.loaded)
test:ok(#loaded_luarocks_cfg > 0, 'luarocks config modules loaded')
test:diag(diag_msg('luarocks.core.cfg modules', loaded_luarocks_cfg))

os.exit(test:check() and 0 or 1)
