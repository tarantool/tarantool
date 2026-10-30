#!/usr/bin/env tarantool

--
-- gh-4231: box.cfg is another function (so called <load_cfg>)
-- before box is loaded. Usually a user calls box.cfg({<...>}),
-- it configures box and replaces box.cfg implementation to one
-- that performs box reconfiguration: so further calls to
-- box.cfg({<...>}) reconfigure box.
--
-- However it is possible to save box.cfg value (<load_cfg>)
-- before box loading and call it after box loading. The behaviour
-- should be the same as for box.cfg call: box should be
-- reconfigured.
--

local tap = require('tap')
local test = tap.test('gh-4231-box-cfg-idempotence')
test:plan(4)

local load_cfg = box.cfg

box.cfg{}

-- This call should be successful and should reinitialize box.
local ok, res = pcall(load_cfg, {read_only = true})
test:ok(ok, 'verify load_cfg after box.cfg() call', {err = res})
test:is(box.cfg.read_only, true, 'verify that load_cfg reconfigures box')

-- Just in case: verify usual box.cfg() after load_cfg().
local ok, res = pcall(box.cfg, {read_only = false})
test:ok(ok, 'verify box.cfg() after load_cfg()', {err = res})
test:is(box.cfg.read_only, false, 'verify that box.cfg() reconfigures box')

os.exit(test:check() and 0 or 1)
