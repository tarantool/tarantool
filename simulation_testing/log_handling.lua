local xlog = require('xlog')
local fio = require('fio')
local fiber = require('fiber')
local randomized_operations = require("randomized_operations")

-- Function for reading the xlog
local function read_xlog(file_path)

    local file_stat = fio.stat(file_path)
    if not file_stat then
        error("File does not exist: " .. file_path)
    end
    if file_stat:is_dir() then
        error("Provided file_path is a directory, not a file: " .. file_path)
    end

    local xlog_entries = {}
    for _, entry in xlog.pairs(file_path) do
        table.insert(xlog_entries, setmetatable(entry, { __serialize = "map" }))
    end

    return xlog_entries
end

--Function of getting the latest xlog in the directory
local function get_latest_xlog(wal_dir)
    local latest_xlog = nil
    local latest_time = 0

    local xlog_files = fio.glob(wal_dir .. '/*.xlog')
    if not xlog_files or #xlog_files == 0 then
        return nil, "No .xlog files in the directory: " .. wal_dir
    end

    for _, file in ipairs(xlog_files) do
        local file_info = fio.stat(file)
        if file_info and file_info.mtime > latest_time then
            latest_time = file_info.mtime
            latest_xlog = file
        end
    end

    return latest_xlog
end

-- Function for processing RAFT logs and constructing the final state
local function process_raft_log(xlog_entries)
    local state = {} -- Итоговая таблица с подтвержденными изменениями
    local pending_transactions = {} -- Таблица с ожидающими изменениями

    for _, entry in ipairs(xlog_entries) do
        local header = entry.HEADER
        local body = entry.BODY
        local request_type = header.type
        local transaction_id = header.lsn 

        if request_type == "RAFT_CONFIRM" then
            -- Если это подтверждение транзакции
            local confirmed_lsn = body[3]  
            if confirmed_lsn and pending_transactions[confirmed_lsn] then
                -- Перемещаем из таблицы ожидания в итоговую
                state[confirmed_lsn] = pending_transactions[confirmed_lsn]
                pending_transactions[confirmed_lsn] = nil  -- Удаляем из ожидания
            end

        elseif request_type == "RAFT_PROMOTE" then
            -- Если это промоция транзакции
            if pending_transactions[transaction_id] then
                -- Перемещаем из таблицы ожидания в итоговую
                state[transaction_id] = pending_transactions[transaction_id]
                pending_transactions[transaction_id] = nil  -- Удаляем из ожидания
            end

        elseif request_type == "RAFT_DEMOTE" then
            -- Если это демоция транзакции
            if state[transaction_id] then
                -- Перемещаем из итоговой таблицы обратно в таблицу ожидания
                pending_transactions[transaction_id] = state[transaction_id]
                state[transaction_id] = nil  -- Удаляем из итоговой
            end

        elseif request_type == "RAFT_ROLLBACK" then
            -- Если это откат транзакции
            local rollback_lsn = body[3]  
            if rollback_lsn then
                -- Откатываем изменения в обеих таблицах до нужного значения
                -- Удаляем все записи после rollback_lsn
                for k, _ in pairs(state) do
                    if k >= rollback_lsn then
                        state[k] = nil
                    end
                end
                for k, _ in pairs(pending_transactions) do
                    if k >= rollback_lsn then
                        pending_transactions[k] = nil
                    end
                end
            end

        else
            -- Если это новая транзакция, добавляем её в таблицу ожидания
            pending_transactions[transaction_id] = entry
        end

    end

    return state
end

-- The simplest function for comparing two logs
local function compare_xlogs(xlog1_path, xlog2_path)

    local function extract_node_id(path)
        local match = path:match("replica_(%d+)")
        return match and tonumber(match) or nil
    end

    local function process_and_sort_log(file_path)
        
        local xlog_entries = read_xlog(file_path)
        local processed_log = process_raft_log(xlog_entries)

        local sorted_entries = {}
        for key, value in pairs(processed_log) do
            table.insert(sorted_entries, { key = key, value = value })
        end

        table.sort(sorted_entries, function(a, b)
            return a.key < b.key
        end)

        return sorted_entries
    end

    local node1_id = extract_node_id(xlog1_path)
    local node2_id = extract_node_id(xlog2_path)

    if not node1_id or not node2_id then
        error("Couldn't extract node_id from paths: " .. xlog1_path .. " and " .. xlog2_path)
    end

    local processed_log1 = process_and_sort_log(xlog1_path)
    local processed_log2 = process_and_sort_log(xlog2_path)

    local differences = {}
    local max_len = math.max(#processed_log1, #processed_log2)

    for i = 1, max_len do
        local entry1 = processed_log1[i]
        local entry2 = processed_log2[i]

        if not entry1 then
            table.insert(differences, string.format("Log1 is missing entry at index %d", i))
        elseif not entry2 then
            table.insert(differences, string.format("Log2 is missing entry at index %d", i))
        else
            local yaml1 = require('yaml').encode(entry1)
            local yaml2 = require('yaml').encode(entry2)
            if yaml1 ~= yaml2 then
                table.insert(differences, string.format(
                    "Mismatch in processed entries at index %d:\nLog1: %s\nLog2: %s",
                    i,
                    yaml1,
                    yaml2
                ))
            end
        end
    end

    if #differences == 0 then
        print(string.format(
            "[XLOG MONITOR][Node %d][Node %d] All processed entries in the logs are identical",
            node1_id, node2_id
        ))
    else
        print(string.format(
            "[XLOG MONITOR][Node %d][Node %d] Differences found between processed entries in the logs:",
            node1_id, node2_id
        ))
        for _, diff in ipairs(differences) do
            print(string.format(
                "[XLOG MONITOR][Node %d][Node %d][Diff] %s",
                node1_id, node2_id,
                diff
            ))
        end
    end
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
        local success, err = pcall(compare_xlogs, latest_log_1, latest_log_2)
        if not success then
            return nil, "Error comparing logs: " .. tostring(err)
        end
        return true
    else
        return nil, "Could not find .xlog files for one or both replicas"
    end
end


local function periodic_insert(node, space_name, i_0, step, interval)
    fiber.create(function()
        local key = i_0
        while true do
            local value = string.format("Value for key %s", key)
            local operation = {"insert", {key, value}}
            
            randomized_operations.execute_db_operation(node, space_name, operation)

            key = key + step
            fiber.sleep(interval)
        end
    end)
end

local function get_last_n_entries(node, space_name, n)
    local space = box.space[space_name]
    if not space then
        return nil, string.format("Space '%s' does not exist on this node.", space_name)
    end

    local max_key = space.index.primary:max()
    if not max_key then
        return nil, "There is not max_key"
    end

    local operation = {
        "select",
        {max_key, {iterator = "LE", limit = n}}
    }

    local result = randomized_operations.execute_db_operation(node, space_name, operation)

    if not result or #result == 0 then
        return nil, "No records found"
    end

    return result
end

local function write_to_file(file_name, content)
    local file, err = io.open(file_name, "a")
    if not file then
        print("Error opening file: " .. err)
        return
    end
    file:write(content .. "\n")
    file:close()
end

local function find_common_prefix(arrays)
    local prefix = {}
    local max_length = math.min(unpack(map(arrays, function(array) return #array end)))  -- Найдем минимальную длину среди всех массивов

    for i = 1, max_length do
        local value = arrays[1][i]
        local all_match = true

        -- Проверяем, совпадает ли значение во всех массивах
        for _, array in ipairs(arrays) do
            if array[i] ~= value then
                all_match = false
                break
            end
        end

        -- Если все элементы совпадают, добавляем их в префикс
        if all_match then
            table.insert(prefix, value)
        else
            break
        end
    end

    return prefix
end

function map(t, f)
    local res = {}
    for i, v in ipairs(t) do
        res[i] = f(v)
    end
    return res
end

local function compare_last_n_entries(nodes, space_name, n, output_file)
    local entries_by_node = {}
    local prefix_match_count = 0
    local differences = {}

    -- Getting the last n entries for each node
    for _, node in ipairs(nodes) do
        local success, result = pcall(function()
            return get_last_n_entries(node, space_name, n)
        end)

        if success then
            if result then
                entries_by_node[node.alias] = result
            else
                print(string.format("[RW MONITOR] No entries found for node '%s'.", node.alias))
            end
        else
            print(string.format("[RW MONITOR] Error fetching entries from node '%s': %s", node.alias, result))
        end
    end

    if next(entries_by_node) == nil then
        print("[RW MONITOR] No entries retrieved from any node.")
        return
    end

    local all_entries = {}
    for _, records in pairs(entries_by_node) do
        table.insert(all_entries, records)
    end

    local common_prefix = find_common_prefix(all_entries)

    if not next(differences) then
        print("[RW MONITOR] No differences found.")
    else
        print("[RW MONITOR] Differences found. Detailed results are being written to the file.")

        write_to_file(output_file, string.format("Common prefix length: %d", #common_prefix))
        if #common_prefix > 0 then
            write_to_file(output_file, "Common prefix:")
            for i, value in ipairs(common_prefix) do
                write_to_file(output_file, string.format("[%d]: %s", i, tostring(value)))
            end
        end

        write_to_file(output_file, "Differences found:")
        for i, nodes_diff in pairs(differences) do
            write_to_file(output_file, string.format("At position %d:", i))
            for node_name, record in pairs(nodes_diff) do
                write_to_file(output_file, string.format("  Node '%s': %s", node_name, tostring(record)))
            end
        end
    end
end


--[[
local wal_file_1 = fio.pathjoin('./replicas_dirs/replica_1/wal_dir/00000000000000000008.xlog') 
local wal_file_2 = fio.pathjoin('./replicas_dirs/replica_2/wal_dir/00000000000000000000.xlog') 
local log_entries_1 = read_xlog(wal_file_1)
print("log_entries_1:")
for _, entry in ipairs(log_entries_1) do
    print(require('yaml').encode(entry))
end

local log_entries_2 = read_xlog(wal_file_2)
print("log_entries_2:")
for _, entry in ipairs(log_entries_2) do
    print(require('yaml').encode(entry))
end

print("go to processing\n")
local final_state1 = process_raft_log(log_entries_1)
local final_state2 = process_raft_log(log_entries_2)

print("Final State 1:")
for key, value in pairs(final_state1) do
    print(key, require('yaml').encode(value))
end

print("Final State 2:")
for key, value in pairs(final_state2) do
    print(key, require('yaml').encode(value))
end

compare_xlogs(wal_file_1, wal_file_2)

]]--

return {
    read_xlog = read_xlog,
    get_latest_xlog = get_latest_xlog,
    process_raft_xlog = process_raft_xlog,
    compare_xlogs = compare_xlogs,
    compare_two_random_xlogs = compare_two_random_xlogs,
    periodic_insert = periodic_insert,
    compare_last_n_entries = compare_last_n_entries,


}