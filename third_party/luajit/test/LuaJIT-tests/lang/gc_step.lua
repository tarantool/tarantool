local function testgc(what, func)
  collectgarbage()
  local oc = gcinfo()
  func()
  local nc = gcinfo()
  assert(nc < oc * 4, "GC step missing for " .. what)
end

do --- TNEW
  testgc("TNEW", function()
    for _ = 1, 10000 do
      local _ = {}
    end
  end)
end

do --- TDUP
  testgc("TDUP", function()
    for _ = 1, 10000 do
      local _ = {1}
    end
  end)
end

do --- FNEW
  testgc("FNEW", function()
    for _ = 1, 10000 do
      local function _() end
    end
  end)
end

do --- CAT
  testgc("CAT", function()
    for i = 1, 10000 do
      local _ = "x" .. i
    end
  end)
end
