-- tracer.lua -- a module for tracing.
-- It creates spans by function call_span(name, func, args).
-- It records selected spans (name, id, parent_id, start/end time)
-- by user-defined callback set via on_span_end().
-- The decision on whether or not to keep a trace is made once,
-- at the root span, based on a probability set
-- by the `set_trace_probability(p)` function. Introduced in gh-12998.

local fiber = require('fiber')
local digest = require('digest')
local ffi = require('ffi')
local clock = require('clock')
local utils = require('internal.utils')
local log = require('log')

local trace_probability = 0
local UNRECORDED_SPAN_ID = 1ULL

local function set_trace_probability(p)
    utils.check_param(p, "probability", "number")
    if not (p >= 0 and p <= 1) then
        error('probability must be between 0 and 1', 0)
    end
    trace_probability = p
end

local function get_trace_probability()
    return trace_probability
end

local write_span = nil

local function on_span_end(func)
    if func ~= nil then
        utils.check_param(func, "func", "function")
    end
    write_span = func
end

local function create_span(name, parent_span_id)
    local bytes = digest.urandom(8)
    local buf = ffi.new("uint64_t[1]")
    ffi.copy(buf, bytes, 8)
    local span = {
        id = buf[0],
        parent_id = parent_span_id,
        name = name,
        start_time = clock.realtime64(),
        end_time = 0,
    }
    if span.id == 0ULL or span.id == 1ULL then
        span.id = span.id + 2ULL
    end
    return span
end

local function call_span(name, func, ...)
    utils.check_param(name, "name", "string")
    utils.check_param(func, "func", "function")
    local f = fiber.self()
    local parent_span_id = fiber._internal.get_span_id(f)
    local trace_enable = true
    if parent_span_id == 0 then
        if math.random() >= trace_probability then
            trace_enable = false
        end
    else
        if parent_span_id == 1 then
            return func(...)
        end
    end

    local id
    local span
    if trace_enable then
        span = create_span(name, parent_span_id)
        id = span.id
    else
        id = UNRECORDED_SPAN_ID
    end

    fiber._internal.set_span_id(f, id)
    local results = utils.table_pack(pcall(func, ...))

    if trace_enable then
        span.end_time = clock.realtime64()
    end

    fiber._internal.set_span_id(f, parent_span_id)

    if trace_enable then
        if write_span then
            local ok, err = pcall(write_span, span)
            if not ok then
                log.error('tracer: error during record span: %s', err)
            end
        end
    end

    if not results[1] then
        error(results[2], 0)
    end
    return unpack(results, 2, results.n)
end

return {
    call_span = call_span,
    on_span_end = on_span_end,
    set_trace_probability = set_trace_probability,
    get_trace_probability = get_trace_probability,
}
