-- tracer.lua (internal file)

local fiber = require('fiber')
local digest = require('digest')
local ffi = require('ffi')
local clock = require('clock')
local log = require('log')
local utils = require('internal.utils')

local trace_enabled = false
local trace_probability = 1.1

local function tracer_disable()
    trace_enabled = false
end

local function tracer_enable()
    trace_enabled = true
end

local function set_trace_probability(p)
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
    return span
end

local function call_span(name, func, ...)
    local f = fiber.self()
    local parent_span_id = fiber._internal.get_span_id(f)
    local trace_flag = fiber._internal.get_trace_flag(f)
    if (not trace_enabled and parent_span_id == 0) or not trace_flag then
        return func(...)
    end
    if parent_span_id == 0 then
        if math.random() > trace_probability then
            fiber._internal.set_trace_flag(f, false)
            return func(...)
        end
    end
    local span = create_span(name, parent_span_id)
    fiber._internal.set_span_id(f, span.id)
    local results = utils.table_pack(pcall(func, ...))
    span.end_time = clock.monotonic()
    if write_span then
        write_span(span)
    end
    fiber._internal.set_span_id(f, parent_span_id)
    if not results[1] then
        error(results[2])
    end
    return unpack(results, 2, results.n)
end

return {
    call_span = call_span,
    enable = tracer_enable,
    disable = tracer_disable,
    on_span_end = on_span_end,
    set_trace_probability = set_trace_probability,
    get_trace_probability = get_trace_probability,
}
