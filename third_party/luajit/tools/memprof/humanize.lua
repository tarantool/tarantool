-- Simple human-readable renderer of LuaJIT's memprof profile.
--
-- Major portions taken verbatim or adapted from the LuaVela.
-- Copyright (C) 2015-2019 IPONWEB Ltd.

local M = {}

local function human_readable_bytes(bytes)
  local units = {"B", "KiB", "MiB", "GiB"}
  local magnitude = 1

  while bytes >= 1024 and magnitude < #units do
    bytes = bytes / 1024
    magnitude = magnitude + 1
  end
  local is_int = math.floor(bytes) == bytes
  local fmt = is_int and "%d%s" or "%.2f%s"

  return string.format(fmt, bytes, units[magnitude])
end

local function format_bytes(bytes, config)
  if config.human_readable then
    return human_readable_bytes(bytes)
  else
    return string.format('%dB', bytes)
  end
end

function M.render(events, config)
  local ids = {}

  for id, _ in pairs(events) do
    table.insert(ids, id)
  end

  table.sort(ids, function(id1, id2)
    return events[id1].num > events[id2].num
  end)

  for i = 1, #ids do
    local event = events[ids[i]]
    print(string.format("%s: %d events\t+%s\t-%s",
      event.loc,
      event.num,
      format_bytes(event.alloc, config),
      format_bytes(event.free, config)
    ))

    local prim_loc = {}
    for _, heap_chunk in pairs(event.primary) do
      table.insert(prim_loc, heap_chunk.loc)
    end
    if #prim_loc ~= 0 then
      table.sort(prim_loc)
      print("\tOverrides:")
      for j = 1, #prim_loc do
        print(string.format("\t\t%s", prim_loc[j]))
      end
      print("")
    end
  end
end

function M.profile_info(events, config)
  print("ALLOCATIONS")
  M.render(events.alloc, config)
  print("")

  print("REALLOCATIONS")
  M.render(events.realloc, config)
  print("")

  print("DEALLOCATIONS")
  M.render(events.free, config)
  print("")
end

function M.leak_info(dheap, config)
  local leaks = {}
  for line, info in pairs(dheap) do
    -- Report "INTERNAL" events inconsistencies for profiling
    -- with enabled jit.
    if info.dbytes > 0 then
      table.insert(leaks, {line = line, dbytes = info.dbytes})
    end
  end

  table.sort(leaks, function(l1, l2)
    return l1.dbytes > l2.dbytes
  end)

  print("HEAP SUMMARY:")
  for _, l in pairs(leaks) do
    print(string.format(
      "%s holds %s: %d allocs, %d frees",
      l.line,
      format_bytes(l.dbytes, config),
      dheap[l.line].nalloc,
      dheap[l.line].nfree
    ))
  end
  print("")
end

function M.aliases(symbols)
  if #symbols.alias == 0 then return end
  print("ALIASES:")
  for _, source in ipairs(symbols.alias) do
    print(symbols.alias[source]..":")
    local lineno = 1
    for line in source:gmatch("([^\n]+)") do
      print(string.format("%d\t| %s", lineno, line))
      lineno = lineno + 1
    end
    print("\n")
  end
end

return M
