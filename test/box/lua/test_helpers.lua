local test_helpers = {}

local _hide = {
    pid_file=1, log=1, listen=1, vinyl_dir=1,
    memtx_dir=1, wal_dir=1,
    memtx_max_tuple_size=1, memtx_min_tuple_size=1
}

function test_helpers.cfg_filter(data)
    if type(data)~='table' then return data end
    local keys,k,_ = {}
    for k in pairs(data) do
        table.insert(keys, k)
    end
    table.sort(keys)
    local result = {}
    for _,k in pairs(keys) do
        table.insert(result, {k, _hide[k] and '<hidden>' or test_helpers.cfg_filter(data[k])})
    end
    return result
end

function compare(a,b)
    return a[1] < b[1]
end

function test_helpers.sorted(data)
    table.sort(data, compare)
    return data
end

return test_helpers
