local xlog = require('xlog')
local fio = require('fio')

-- Function for reading the log
local function read_xlog(file_path)

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

local function get_committed_entries(log)
    local committed = {}
    for _, entry in ipairs(log) do
        if entry.committed then
            table.insert(committed, entry)
        end
    end
    return committed
end

-- The simplest function for comparing two logs
local function compare_xlogs(log1_path, log2_path)

    local function extract_node_id(path)
        local match = path:match("replica_(%d+)")
        return match and tonumber(match) or nil
    end

    local node1_id = extract_node_id(log1_path)
    local node2_id = extract_node_id(log2_path)

    if not node1_id or not node2_id then
        error("Couldn't extract node_id from paths: " .. log1_path .. " and " .. log2_path)
    end

    local committed_log1 = get_committed_entries(read_xlog(log1_path))
    local committed_log2 = get_committed_entries(read_xlog(log2_path))

    local differences = {}
    local max_len = math.max(#committed_log1, #committed_log2)

    for i = 1, max_len do
        local entry1 = committed_log1[i]
        local entry2 = committed_log2[i]

        if not entry1 then
            table.insert(differences, string.format("Log1 is missing committed entry at index %d", i))
        elseif not entry2 then
            table.insert(differences, string.format("Log2 is missing committed entry at index %d", i))
        else
            local yaml1 = require('yaml').encode(entry1)
            local yaml2 = require('yaml').encode(entry2)
            if yaml1 ~= yaml2 then
                table.insert(differences, string.format(
                    "Mismatch in committed entries at index %d:\nLog1: %s\nLog2: %s",
                    i,
                    yaml1,
                    yaml2
                ))
            end
        end
    end

    if #differences == 0 then
        print(string.format(
            "[XLOG MONITOR][Node %d][Node %d] All committed entries in the logs are identical",
            node1_id, node2_id
        ))
    else
        print(string.format(
            "[XLOG MONITOR][Node %d][Node %d] Differences found between committed entries in the logs:",
            node1_id, node2_id
        ))
        for _, diff in ipairs(differences) do
            print(string.format(
            "[XLOG MONITOR][Node %d][Node %d][Diff] ",
            node1_id, node2_id
        )..tostring(diff))
        end
    end
end

local function get_latest_xlog(wal_dir)
    local latest_log = nil
    local latest_time = 0

    local xlog_files = fio.glob(wal_dir .. '/*.xlog')
    if not xlog_files or #xlog_files == 0 then
        return nil, "No .xlog files in the directory: " .. wal_dir
    end

    for _, file in ipairs(xlog_files) do
        local file_info = fio.stat(file)
        if file_info and file_info.mtime > latest_time then
            latest_time = file_info.mtime
            latest_log = file
        end
    end

    return latest_log
end

local function compare_two_random_xlogs(replicas_dir_path)

    local replica_dirs_list = {}
    for _, entry in ipairs(fio.glob(replicas_dir_path .. '/replica_*')) do
        if fio.path.is_dir(entry) then
            table.insert(replica_dirs_list, entry)
        end
    end

    if #replica_dirs_list < 2 then
        return nil, "There are not enough replicas to compare"
    end

    local idx1, idx2
    repeat
        idx1 = math.random(1, #replica_dirs_list)
        idx2 = math.random(1, #replica_dirs_list)
    until idx1 ~= idx2 

    local replica_path_1 = replica_dirs_list[idx1]
    local replica_path_2 = replica_dirs_list[idx2]


    local latest_log_1 = get_latest_xlog(replica_path_1 .. '/wal_dir')
    local latest_log_2 = get_latest_xlog(replica_path_2 .. '/wal_dir')

    if latest_log_1 and latest_log_2 then
        return compare_xlogs(latest_log_1, latest_log_2)
    else
        return nil, "Could not find .xlog files for one or both replicas"
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
    read_xlog = read_xlog,
    get_committed_entries = get_committed_entries,
    compare_xlogs = compare_xlogs,
    get_latest_xlog = get_latest_xlog,
    compare_two_random_xlogs = compare_two_random_xlogs

}