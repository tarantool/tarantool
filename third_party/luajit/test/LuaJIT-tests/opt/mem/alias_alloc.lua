do --- ALOAD forwarding, same table.
  local t = {1}
  local x
  for i = 1, 100 do
    local v = {i}
    t[1] = v[1]
    x = v[1]
  end
  assert(x == 100 and t[1] == 100)
end

do --- ALOAD forwarding, different tables.
  local t = {1}
  local x,y
  for i = 1, 100 do
    local v = {i}
    local w = {i + 1}
    x = v[1]
    y = w[1]
  end
  assert(x == 100 and y == 101)
end

do --- FLOAD forwarding.
  local mt = {}
  local t = setmetatable({}, mt)
  local x
  for _ = 1, 100 do
    local v = {}
    setmetatable(v, getmetatable(t))
    assert(getmetatable(v) == mt)
  end
end

-- See also <opt/sink/alloc.lua>.
do --- Forwarding the constant-on-trace table in the complex add.
  local x, k = {1, 2}, {3, 4}
  for _ = 1, 100 do x = {x[1] + k[1], x[2] + k[2]} end
  assert(x[1] == 301)
  assert(x[2] == 402)
end


do --- FLOAD forwarding for tab.asize/tab.array crossing NEWREF.
  local t = {1}
  for _ = 1, 100 do
    local v = {}
    local w = {}
    v[1] = t[1]
    w[1] = t[1]
  end
end
