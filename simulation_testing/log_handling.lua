local xlog = require('xlog')
local fio = require('fio')

-- Function for reading the log
local function read_log(file_path)

    local file_stat = fio.stat(file_path)
    if not file_stat then
        error("File does not exist: " .. file_path)
    end
    if file_stat:is_dir() then
        error("Provided file_path is a directory, not a file: " .. file_path)
    end

    local log_entries = {}
    for _, entry in xlog.pairs(file_path) do
        table.insert(log_entries, setmetatable(entry, { __serialize = "map" }))
    end

    return log_entries
end

-- The simplest function for comparing two logs
local function compare_logs(log1_path, log2_path)
    local log1 = read_log(log1_path)
    local log2 = read_log(log2_path)

    local differences = {}
    local max_len = math.max(#log1, #log2)

    for i = 1, max_len do
        local entry1 = log1[i]
        local entry2 = log2[i]

        if not entry1 then
            table.insert(differences, string.format("Log1 is missing entry at index %d", i))
        elseif not entry2 then
            table.insert(differences, string.format("Log2 is missing entry at index %d", i))
        elseif not require('yaml').encode(entry1) == require('yaml').encode(entry2) then
            table.insert(differences, string.format(
                "Mismatch at index %d:\nLog1: %s\nLog2: %s",
                i,
                require('yaml').encode(entry1),
                require('yaml').encode(entry2)
            ))
        end
    end

    if #differences == 0 then
        print("Logs are identical")
    else
        print("Differences found between logs:")
        for _, diff in ipairs(differences) do
            print(diff)
        end
    end
end

--[[
local wal_file_1 = fio.pathjoin('./replicas_dirs/replica_1/wal_dir/00000000000000000000.xlog') 
local wal_file_2 = fio.pathjoin('./replicas_dirs/replica_2/wal_dir/00000000000000000005.xlog')
local log_entries_1 = read_log(wal_file_1)
local log_entries_2 = read_log(wal_file_2)

print("log_entries_1:")
for _, entry in ipairs(log_entries_1) do
    print(require('yaml').encode(entry))
end

print("log_entries_2:")
for _, entry in ipairs(log_entries_2) do
    print(require('yaml').encode(entry))
end

compare_logs(wal_file_1, wal_file_2)
]]

return {
    read_log = read_log,
    compare_logs = compare_logs
}