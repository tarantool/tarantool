return function(f, ...)
  if pcall(f, ...) ~= false then error("failure expected", 2) end
end
