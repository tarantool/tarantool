local bit = require("bit")
local table = require("table")
local string = require("string")
local floor = require("math").floor

local function s_to_a(data)
    local a = {}
    local bytes = {string.byte(data, i, #data)}
    for i = 1, #data / 8 do
	a[i] = 0
	for c = 0, 7 do
	    a[i] = bit.lshift(a[i], 8)
	    a[i] = a[i] + bytes[i * 8 - c]
	end
    end
    return a
end

local function a_to_s(a)
    local bytes = {}
    for i = 1, #a do
	for c = 0, 7 do
	    table.insert(bytes, bit.band(a[i], 0xff))
	    a[i] = bit.rshift(a[i], 8)
	end
    end
    return string.char(unpack(bytes))
end

local limit = 500
local function amerge(a, b)
    local r = {}
    local n = limit
    while #a > 0 and #b > 0 and n > 0 do
	if a[1] > b[1] then
	    table.insert(r, table.remove(a, 1))
	else
	    table.insert(r, table.remove(b, 1))
	end
	n = n - 1
    end
    while #a > 0 and n > 0 do
	table.insert(r, table.remove(a, 1))
                n = n - 1
    end
    while #b > 0 and n > 0 do
	table.insert(r, table.remove(b, 1))
	n = n - 1
    end
    return r
end

local function afind_ge(a, x)
    if #a == 0 then
	return 1
    end
    
    local first, last  = 1, #a
    local mid
    repeat
	mid = floor(first + (last - first) / 2)
	if x > a[mid] then
	    last = mid
	else
	    first = mid + 1
	end
    until first >= last

    if a[mid] >= x then
	mid = mid + 1
    end

    return mid
end

local function ains(a, key)
    key = tonumber(key)
    
    local i = afind_ge(a, key)
    if a[i] and a[i] >= key then
	table.insert(a, i + 1, key) -- next to equal or greater
    else
	table.insert(a, i, key)
    end
    
    while #a > limit do
	table.remove(a)
    end
end

local function adel(a, key)
    key = tonumber(key)
    local i = afind_ge(a, key)
    if a[i] == key then
	table.remove(a, i)
    end
end

local function get(space, key)
    local tuple = box.select(space, 0, key)
    if tuple then
	return s_to_a(tuple[1])
    else
	return {}
    end
end

local function store(space, key, a)
    return box.replace(space, key, a_to_s(a))
end


function box.sa_insert(space, key, value)
    local a = get(space, key)
    ains(a, value)
    print(unpack(a))
    return store(space, key, a)
end

function box.sa_delete(space, key, ...)
    local a = get(space, key)
    for i, d in pairs({...}) do
	adel(a, d)
    end
    return store(space, key, a)
end

function box.sa_select(space, key, from, limit)
    local a = get(space, key)

    if from ~= nil then
	from = afind_ge(a, tonumber(from))
    else
	from = 1
    end
    
    if limit ~= nil then
	limit = tonumber(limit)
    else
	limit = 0
    end

    local r = {}
    for i = from, #a do
	if a[i] == nil then
	    break
	end
	table.insert(r, a[i])
	limit = limit - 1
	if limit == 0 then
	    break
	end
    end
    return key, r
end

function box.sa_merge(space, key_a, key_b)
    local a = get(space, key_a)
    local b = get(space, key_b)
    local r = amerge(a, b)
    return r
end
