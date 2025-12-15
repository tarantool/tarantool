local M = {}

local bc = require('jit.bc')
local jutil = require('jit.util')
local vmdef = require('jit.vmdef')
local bcnames = vmdef.bcnames
local band, rshift = bit.band, bit.rshift

function M.hasbc(f, bytecode)
  assert(type(f) == 'function', 'argument #1 should be a function')
  assert(type(bytecode) == 'string', 'argument #2 should be a string')
  local function empty() end
  local hasbc = false
  -- Check the bytecode entry line by line.
  local out = {
    write = function(out, line)
      if line:match(bytecode) then
        hasbc = true
        out.write = empty
      end
    end,
    flush = empty,
    close = empty,
  }
  bc.dump(f, out)
  return hasbc
end

-- Get traceno of the trace associated for the given function.
function M.gettraceno(func)
  assert(type(func) == 'function', 'argument #1 should be a function')

  -- The 0th BC is the header.
  local func_ins = jutil.funcbc(func, 0)
  local BC_NAME_LENGTH = 6
  local RD_SHIFT = 16

  -- Calculate index in `bcnames` string.
  local op_idx = BC_NAME_LENGTH * band(func_ins, 0xff)
  -- Get the name of the operation.
  local op_name = string.sub(bcnames, op_idx + 1, op_idx + BC_NAME_LENGTH)
  assert(op_name:match('JFUNC'),
         'The given function has non-jitted header: ' .. op_name)

  -- RD contains the traceno.
  return rshift(func_ins, RD_SHIFT)
end

return M
