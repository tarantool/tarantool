-- LuaJIT's memory profile post-processing module.

local M = {}

function M.form_heap_delta(events)
  -- Auto resurrects source event lines for counting/reporting.
  local dheap = setmetatable({}, {__index = function(t, line)
    rawset(t, line, {
      dbytes = 0,
      nalloc = 0,
      nfree = 0,
    })
    return t[line]
  end})

  for _, event in pairs(events.alloc) do
    if event.loc then
      local ev_line = event.loc

      if (event.alloc > 0) then
        dheap[ev_line].dbytes = dheap[ev_line].dbytes + event.alloc
        dheap[ev_line].nalloc = dheap[ev_line].nalloc + event.num
      end
    end
  end

  -- Realloc and free events are pretty the same.
  -- We aren't interested in aggregated alloc/free sizes for
  -- the event, but only for new and old size values inside
  -- alloc-realloc-free chain. Assuming that we have
  -- no collisions between different object addresses.
  local function process_non_alloc_events(events_by_type)
    for _, event in pairs(events_by_type) do
      -- Realloc and free events always have key named "primary"
      -- that references the table with memory changed
      -- (may be empty).
      for _, heap_chunk in pairs(event.primary) do
        local ev_line = heap_chunk.loc

        if (heap_chunk.allocated > 0) then
          dheap[ev_line].dbytes = dheap[ev_line].dbytes + heap_chunk.allocated
          dheap[ev_line].nalloc = dheap[ev_line].nalloc + heap_chunk.count
        end

        if (heap_chunk.freed > 0) then
          dheap[ev_line].dbytes = dheap[ev_line].dbytes - heap_chunk.freed
          dheap[ev_line].nfree = dheap[ev_line].nfree + heap_chunk.count
        end
      end
    end
  end
  process_non_alloc_events(events.realloc)
  process_non_alloc_events(events.free)
  return dheap
end

return M
