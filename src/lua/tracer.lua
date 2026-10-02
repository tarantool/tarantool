-- tracer.lua (internal file)

local tracer = require('tracer.lib')
local fiber = require('fiber')
local trace_enabled = false

local function tracer_disable()
    trace_enabled = false
end

local function tracer_enable()
    trace_enabled = true
end

local write_span = tracer.write_span

local function on_span_end(func)
    write_span = func
end

local function pack(...)
    return { n = select("#", ...), ... }
end

local function call_span(name, func, ...)
    if not trace_enabled then
        return func(...)
    end
    local f = fiber.self()
    local traceparent = fiber._internal.get_traceparent(f)
    local span = tracer.start_span(name, traceparent)
    local traceparent_new = traceparent:sub(1, 36) .. tracer.get_id(span) ..
                            traceparent:sub(53, 55)
    fiber._internal.set_traceparent(f, traceparent_new)
    local results = pack(pcall(func, ...))
    tracer.end_span(span)
    write_span(span)
    tracer.destroy_span(span)
    fiber._internal.set_traceparent(f, traceparent)
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
}
