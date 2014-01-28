-- bench.lua

return (function()

local ffi = require 'ffi'
ffi.cdef[[
    void ev_now_update(void);
]]

local function ev_now_update()
    ffi.C.ev_now_update()
end

local function errorf(fmt, ...)
    error(string.format(fmt, ...))
end
local function floatstr(f)
    return string.format('%.8s', string.format('%3.8f', f))
end


box.bench = {
    timethese = function(count, ...)
        local t = { ... }
        local result = {}
        for i = 1, #t do
            if math.fmod(i, 2) == 1 then
                table.insert(result, box.bench.timethis(count, t[i + 1], t[i]))
            end
        end

        setmetatable(result, {
            __tostring = function(rr)
                local str = string.format(
                    "\n%-16s %-8s %-8s %-8s %-8s %-8s %8s\n",

                    'Name',
                    'Count',
                    'Time',

                    'Min-time',
                    'Max-time',
                    'Avg-time',

                    'RPS'
                )
                for i = 1, #rr do
                    str = str .. tostring(rr[i]) .. "\n"
                end
                return str
            end
        })
        return result
    end,

    cmpthese = function(count, ...)
        local res = box.bench.timethese(count, ...)

        local result = {}

        for i = 1, #res do
            local line = {res[i].name, string.format('%i/s', res[i].rps) }
            for j = 1, #res do
                if j == i then
                    table.insert(line, '--')
                else
                    local this = res[i].rps
                    local cmp  = res[j].rps

                    if this < cmp then
                        table.insert(line,
                            string.format('-%3.1f%%', (cmp - this) * 100 / this)
                        )
                    else
                        table.insert(line,
                            string.format('%3.1f%%',
                                100 + (this - cmp) * 100 / this)
                        )
                    end
                end
            end
            table.insert(result, line)
        end

        setmetatable(result, {
            __tostring = function(r)
                if #r == 0 then
                    return ''
                end
                local str = "\n"
                str = str .. string.format("%-16s %8s",
                    '-',
                    'Rate'
                )
                for i = 1, #r do
                    str = str .. string.format(
                        "%16s", string.format('%.16s', r[i][1]))
                end
                str = str .. "\n"

                for i = 1, #r do
                    str = str .. string.format("%-16s %-8s",
                        string.format('%.16s', r[i][1]),
                        r[i][2]
                    )

                    for j = 3, #r[i] do
                        str = str .. string.format("%16s", r[i][j])
                    end

                    str = str .. "\n"
                end

                return str
            end
        })

        return result
    end,

    timethis = function(count, code, title)
        if type(code) == 'string' then
            code = box.dostring('return function() ' .. code .. ' end')
        end

        if type(code) ~= 'function' then
            error("usage: box.bench.timethis(count, CODE)")
        end

        if count <= 0 then
            errorf("wrong count value: %s", tostring(count))
        end

        local min = nil
        local max = nil
        local total = 0

        
        for i = 1, count do
            ev_now_update()
            local started = box.time()
            code()
            ev_now_update()
            local iter_time = box.time() - started

            if min == nil then
                min = iter_time
                max = iter_time
                total = iter_time
            else
                total = total + iter_time
                if iter_time < min then
                    min = iter_time
                end
                if iter_time > max then
                    max = iter_time
                end
            end
        end

        local avg = total / count

        local res = {
            name = tostring(title),
            spr  = avg,
            sprmin = min,
            sprmax = max,
            rps  = count / total,
            time = total,
            count = count
        }
    

        setmetatable(res, {
            __tostring = function(r)
                return string.format(
                    '%-16s %-8i %-8s %-8s %-8s %-8s %8i',

                    string.format('%.16s', r.name),
                    r.count,
                    floatstr(r.time),

                    floatstr(r.sprmin),
                    floatstr(r.sprmax),
                    floatstr(r.spr),

                    r.rps
                )
            end
        })
        return res
    end
}


return box.bench

end)()

