-- tracer.lua -- a module for tracing.
-- It creates spans by function call_span(name, func, ...).
-- It records selected spans (name, id, parent_id, start/end time)
-- by user-defined callback set via on_span_end().
-- The decision on whether or not to keep a trace is made once,
-- at the root span, based on a probability set
-- by the `set_trace_probability(p)` function. Introduced in gh-12998.

local fiber = require('fiber')
local ffi = require('ffi')
local clock = require('clock')
local utils = require('internal.utils')
local log = require('log')

local record_span = nil

ffi.cdef([[
    void tracer_set_probability(double p);
    double tracer_get_probability(void);
    int64_t real_random_in_range(int64_t min, int64_t max);
]])

local MAX_INT64 = 223372036854775807

-- Means that the trace has not yet begun.
local NO_SPAN_ID = 0ULL

-- Means that the current trace is marked as one that should not be recorded.
local UNRECORDED_SPAN_ID = 1ULL

local function set_trace_probability(p)
    utils.check_param(p, "probability", "number", 2)
    if not (p >= 0 and p <= 1) then
        box.error(box.error.ILLEGAL_PARAMS,
                  'probability must be between 0 and 1', 2)
    end
    ffi.C.tracer_set_probability(p)
end

local function get_trace_probability()
    return ffi.C.tracer_get_probability()
end

local function on_span_end(func)
    if func ~= nil then
        utils.check_param(func, "func", "function", 2)
    end
    record_span = func
end

local function create_span(name, parent_span_id)
    local span = {
        id = ffi.C.real_random_in_range(2, MAX_INT64),
        parent_id = parent_span_id,
        name = name,
        start_time = clock.realtime64(),
        end_time = 0,
    }
    return span
end

local function call_span(name, func, ...)
    utils.check_param(name, "name", "string", 2)
    utils.check_param(func, "func", "function", 2)
    local f = fiber.self()
    local parent_span_id = fiber._internal.get_span_id(f)
    local trace_enable
    if parent_span_id == NO_SPAN_ID then
        if record_span ~= nil then
            trace_enable = math.random() < ffi.C.tracer_get_probability()
        else
            trace_enable = false
        end
    elseif parent_span_id == UNRECORDED_SPAN_ID then
        return func(...)
    else
        trace_enable = true
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

    fiber._internal.set_span_id(f, parent_span_id)

    if trace_enable then
        span.end_time = clock.realtime64()
        if record_span then
            local ok, err = pcall(record_span, span)
            if not ok then
                log.error('tracer: error while recording span: %s', err)
            end
        end
    end

    if not results[1] then
        local err = results[2]
        if box.error.is(err) then
            box.error(err, 2)
        else
            box.error({reason = tostring(err)}, 2)
        end
    end
    return unpack(results, 2, results.n)
end

return {
    call_span = call_span,
    on_span_end = on_span_end,
    set_trace_probability = set_trace_probability,
    get_trace_probability = get_trace_probability,
}
