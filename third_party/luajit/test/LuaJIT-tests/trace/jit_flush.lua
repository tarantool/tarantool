do --- Flushing a trace that is a link for another trace.
  -- TRACE 3 stop -> 2
  jit.flush()
  for i = 1, 100 do
    if i == 50 then jit.flush(2) end
    for _ = 1, 100 do end
    for _ = 1, 100 do end
  end

  jit.flush()
end

do --- Flushing stitched trace.
  jit.flush()
  local function f() for _ = 1, 100 do end end
  for _ = 1, 100 do local x = gcinfo(); f() end

  jit.flush()
end

do --- Flushing trace with up-recursion.
  jit.flush()

  local function fib(n)
    if n < 2 then return 1 end
    return fib(n - 2) + fib(n - 1)
  end

  fib(11)

  jit.flush()
end

do --- Flush in the loop.
  jit.flush()

  local names = {}
  for i = 1, 100 do names[i] = i end

  local function f()
    for k, v in ipairs(names) do end
  end

  f()

  for _ = 1, 2 do
    f()
    f()
    jit.flush()
  end

  jit.flush()
end

do --- Flushes of not existed traces.
  jit.flush()

  jit.flush(1) -- ignored
  jit.flush(2) -- ignored
  for _ = 1, 1e7 do end -- causes trace #1

  jit.flush(2) -- ignored
  jit.flush(1) -- ok
  jit.flush(1) -- crashes
end
