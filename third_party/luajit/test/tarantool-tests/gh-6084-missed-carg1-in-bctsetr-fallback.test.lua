local tap = require('tap')
local utils = require('utils')

local test = tap.test('gh-6084-missed-carg1-in-bctsetr-fallback')
test:plan(2)

-- XXX: Bytecode TSETR appears only in built-ins libraries, when
-- doing fixups for fast function written in Lua (i.e.
-- `table.move()`), by replacing all TSETV bytecodes with
-- the TSETR. See <src/host/genlibbc.lua> for more details.

-- This test checks that fallback path, when the index of the new
-- set element is greater than the table's asize, doesn't lead
-- to a crash.

-- XXX: We need to make sure the bytecode is present in the chosen
-- built-in to make sure our test is still valid.
assert(utils.frontend.hasbc(table.move, 'TSETR'))

-- `t` table asize equals 1. Just copy its first element (1)
-- to the field by index 2 > 1, to fallback inside TSETR.
local t = {1}
local res = table.move(t, 1, 1, 2)
test:ok(t == res, 'table.move returns the same table')
test:ok(t[1] == t[2], 'table.move is correct')

test:done(true)
