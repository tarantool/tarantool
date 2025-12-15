-- All cases below use constants on the trace.
local nan = 0/0
local t = {}
for i = 1, 100 do t[i] = i + 0.5 end
for i = 101, 200 do t[i] = nan end

do --- Branch is never taken, NaN > 1000.
  local z = 0
  for i = 1, 200 do if t[i] > 1000 then z = i end end
  assert(z == 0)
end

do --- Branch begins to be taken on the trace, not (NaN < 1000).
  local z = 0
  for i = 1, 200 do if not (t[i] < 1000) then z = i end end
  assert(z == 200)
end

do --- Branch ends to be taken on the trace, NaN <= 1000.
  local z = 0
  for i = 1, 200 do if t[i] <= 1000 then z = i end end
  assert(z == 100)
end

do --- Branch is always taken, not (NaN >= 1000).
  local z = 0
  for i = 1, 200 do if not (t[i] >= 1000) then z = i end end
  assert(z == 200)
end

do --- Branch ends to be taken on the trace, NaN > 0.
  local z = 0
  for i = 1, 200 do if t[i] > 0 then z = i end end
  assert(z == 100)
end

do --- Branch is always taken, not (NaN < 0).
  local z = 0
  for i = 1, 200 do if not (t[i] < 0) then z = i end end
  assert(z == 200)
end

do --- Branch is never taken, NaN <= 0.
  local z = 0
  for i = 1, 200 do if t[i] <= 0 then z = i end end
  assert(z == 0)
end

do --- Branch begins to be taken on the trace, not (NaN >= 0).
  local z = 0
  for i = 1, 200 do if not (t[i] >= 0) then z = i end end
  assert(z == 200)
end

do --- NaN assign on trace.
  local z
  for _ = 1, 100 do z = 0/0 end
  assert(z ~= z)
end

do --- Nan == NaN.
  local z
  for _ = 1, 100 do z = nan == nan end
  assert(z == false)
end

do --- NaN == 1.
  local z
  for _ = 1, 100 do z = nan == 1 end
  assert(z == false)
end

do --- 1 == NaN.
  local z
  local z
  for _ = 1, 100 do z = 1 == nan end
  assert(z == false)
end

do --- NaN ~= NaN.
  local z
  for _ = 1, 100 do z = nan ~= nan end
  assert(z == true)
end

do --- NaN ~= 1.
  local z
  for _ = 1, 100 do z = nan ~= 1 end
  assert(z == true)
end

do --- 1 ~= NaN.
  local z
  for _ = 1, 100 do z = 1 ~= nan end
  assert(z == true)
end

do --- NaN < NaN.
  local z
  for _ = 1, 100 do z = nan < nan end
  assert(z == false)
end

do --- NaN < 1.
  local z
  for _ = 1, 100 do z = nan < 1 end
  assert(z == false)
end

do --- 1 < NaN.
  local z
  for _ = 1, 100 do z = 1 < nan end
  assert(z == false)
end

do --- not (NaN < NaN).
  local z
  for _ = 1, 100 do z = not (nan < nan) end
  assert(z == true)
end

do --- not (NaN < 1).
  local z
  for _ = 1, 100 do z = not (nan < 1) end
  assert(z == true)
end

do --- not (1 < NaN).
  local z
  for _ = 1, 100 do z = not (1 < nan) end
  assert(z == true)
end

do --- NaN > NaN.
  local z
  for _ = 1, 100 do z = nan > nan end
  assert(z == false)
end

do --- NaN > 1.
  local z
  for _ = 1, 100 do z = nan > 1 end
  assert(z == false)
end

do --- 1 > NaN.
  local z
  for _ = 1, 100 do z = 1 > nan end
  assert(z == false)
end

do --- not (NaN > NaN).
  local z
  for _ = 1, 100 do z = not (nan > nan) end
  assert(z == true)
end

do --- not (NaN > 1).
  local z
  for _ = 1, 100 do z = not (nan > 1) end
  assert(z == true)
end

do --- not (1 > NaN).
  local z
  for _ = 1, 100 do z = not (1 > nan) end
  assert(z == true)
end

do --- NaN <= NaN.
  local z
  for _ = 1, 100 do z = nan <= nan end
  assert(z == false)
end

do --- NaN <= 1.
  local z
  for _ = 1, 100 do z = nan <= 1 end
  assert(z == false)
end

do --- 1 <= NaN.
  local z
  for _ = 1, 100 do z = 1 <= nan end
  assert(z == false)
end

do --- not (NaN <= NaN).
  local z
  for _ = 1, 100 do z = not (nan <= nan) end
  assert(z == true)
end

do --- not (NaN <= 1).
  local z
  for _ = 1, 100 do z = not (nan <= 1) end
  assert(z == true)
end

do --- not (1 <= NaN).
  local z
  for _ = 1, 100 do z = not (1 <= nan) end
  assert(z == true)
end

do --- NaN >= NaN.
  local z
  for _ = 1, 100 do z = nan >= nan end
  assert(z == false)
end

do --- NaN >= 1.
  local z
  for _ = 1, 100 do z = nan >= 1 end
  assert(z == false)
end

do --- 1 >= NaN.
  local z
  for _ = 1, 100 do z = 1 >= nan end
  assert(z == false)
end

do --- not (NaN >= NaN).
  local z
  for _ = 1, 100 do z = not (nan >= nan) end
  assert(z == true)
end

do --- not (NaN >= 1).
  local z
  for _ = 1, 100 do z = not (nan >= 1) end
  assert(z == true)
end

do --- not (1 >= NaN).
  local z
  for _ = 1, 100 do z = not (1 >= nan) end
  assert(z == true)
end
