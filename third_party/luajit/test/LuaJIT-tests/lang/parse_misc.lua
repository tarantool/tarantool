do --- Ambiguous syntax: function call vs. new statement. +lua==5.2
  assert(assert(loadstring([[
    local function f() return 99 end
    return f
    ()
  ]]))() == 99)
end

do --- Ambiguous syntax: function call vs. new statement. +lua<5.2
  assert(loadstring([[
    local function f() return 99 end
    return f
    ()
  ]]) == nil)
end

do --- UTF-8 identifiers.
  assert(loadstring([[
  local ä = 1
  local aäa = 2
  local äöü·€晶 = 3

  assert(ä == 1)
  assert(aäa == 2)
  assert(äöü·€晶 == 3)

  assert(#"ä" == 2)
  assert(#"aäa" == 4)
  assert(#"äöü·€晶" == 14)
  ]]))()
end
