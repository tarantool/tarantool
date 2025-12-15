local test = [[
  local testvar = %s
  debug.setmetatable(testvar, {__tostring = function(o)
    return ('__tostring is reloaded for %s'):format(type(o))
  end})
  print(testvar)
]]

pcall(load(test:format(unpack(arg))))
