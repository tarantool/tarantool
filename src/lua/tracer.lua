-- tracer.lua

local fiber = require('fiber')
local digest = require('digest')
local ffi = require('ffi')
local clock = require('clock')
local utils = require('internal.utils')

local trace_probability = 0

local function set_trace_probability(p)
    assert(type(p) ~= 'numeric', 'probability must be numeric')
    assert(p >= 0 and p <= 1, 'probability must be between 0 and 1')
    trace_probability = p
end

local function get_trace_probability()
    return trace_probability
end

local write_span = nil

local function on_span_end(func)
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
    if span.id == 0 then
        span.id = 1
    end
    return span
end

local function call_span(name, func, ...)
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
        id = 1
    end

    fiber._internal.set_span_id(f, id)
    local results = utils.table_pack(pcall(func, ...))

    if trace_flag then
        span.end_time = clock.monotonic()
    end

    fiber._internal.set_span_id(f, parent_span_id)

    if trace_flag then
        if write_span then
            pcall(write_span, span)
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
