local bufread = require('utils.bufread')
local symtab = require('utils.symtab')

local function make_error_handler(fmt, inputfile)
  return function(err)
    io.stderr:write(string.format(fmt, inputfile))
    io.stderr:write(string.format('\n\t%s\n', err))
    os.exit(1, true)
  end
end

return function(parser, inputfile)
  local _, reader = xpcall(
    bufread.new,
    make_error_handler('Failed to open %s.', inputfile),
    inputfile
  )

  local _, symbols = xpcall(
    symtab.parse,
    make_error_handler('Failed to parse symtab from %s.', inputfile),
    reader
  )

  local _, events = xpcall(
    parser,
    make_error_handler('Failed to parse profile data from %s.', inputfile),
    reader,
    symbols
  )
  return events, symbols
end
