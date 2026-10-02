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
local TRACING_ENABLE = 1

local function set_trace_probability(p)
    utils.check_param(p, "probability", "number")
    assert(p >= 0 and p <= 1, 'probability must be between 0 and 1', 0)
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
    local span = {
        id = ffi.cast('uint64_t*', bytes)[0],
        parent_id = parent_span_id,
        name = name,
        start_time = clock.monotonic(),
        end_time = 0,
    }
    if span.id == 0 or span.id == 1 then
        span.id = span.id + 2
    end
    return span
end

local function call_span(name, func, ...)
    utils.check_param(name, "name", "string")
    utils.check_param(func, "func", "function")
    local f = fiber.self()
    local parent_span_id = fiber._internal.get_span_id(f)
    local trace_flag = fiber._internal.get_trace_flag(f)
    if parent_span_id == 0 then
        if math.random() >= trace_probability then
            fiber._internal.set_trace_flag(f, false)
        else
            fiber._internal.set_trace_flag(f, true)
        end
    else
        if not trace_flag then
            return func(...)
        end
    end

    local id
    local span
    trace_flag = fiber._internal.get_trace_flag(f)
    if trace_flag then
        span = create_span(name, parent_span_id)
        id = span.id
    else
        id = TRACING_ENABLE
    end

    fiber._internal.set_span_id(f, id)
    local results = utils.table_pack(pcall(func, ...))

    if trace_flag then
        span.end_time = clock.monotonic()
    end

    fiber._internal.set_span_id(f, parent_span_id)

    if trace_flag then
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
