local fio = require('fio')
local json = require("dkjson")




local function add_error_scenario(data)
    local filename = "./error_scenarios/scenarios.json"
    local file = io.open(filename, "a")
    if file then
        -- Convert data to JSON and append to file
        file:write("{".. json.encode(data) .. "\n}, \n")
        file:close()
        print("Data appended successfully")
    else
        print("Failed to open file for writing")
    end
end

-- File copy function
local function copy_file(src_file, dest_file)
    
    local src_stat = fio.stat(src_file)
    if not src_stat then
        error("File does not exist: " .. src_file)
    end
    if src_stat:is_dir() then
        error("Provided src_file parameter is a directory, not a file: " .. src_file)
    end

    local dest_dir = fio.dirname(dest_file)
    local dest_stat = fio.stat(dest_dir)
    if not dest_stat then
        local success, err = fio.mkdir(dest_dir)
        if not success then
            error("Failed to create directory: " .. dest_dir .. " - " .. err)
        end
        print("Created directory:", dest_dir)
    elseif not dest_stat:is_dir() then
        error("Destination path is not a directory: " .. dest_dir)
    end

    local success, err = fio.copyfile(src_file, dest_file)
    if not success then
        error("Failed to copy file: " .. err)
    end

    print(string.format("File '%s' successfully copied to '%s'", src_file, dest_file))
end

-- File deletion function
local function delete_file(file_path)
 
    local file_stat = fio.stat(file_path)
    if not file_stat then
        error("File does not exist: " .. file_path)
    end
    if file_stat:is_dir() then
        error("Provided file_path parameter is a directory, not a file: " .. file_path)
    end

    local success, err = fio.unlink(file_path)
    if not success then
        error("Failed to delete file: " .. err)
    end

    print(string.format("File '%s' successfully deleted", file_path))
end

-- Recursive directory deletion function
local function delete_directory(directory_path)

    local dir_stat = fio.stat(directory_path)
    if not dir_stat then
        error("Directory does not exist: " .. directory_path)
    end
    if not dir_stat:is_dir() then
        error("Provided directory_path is not a directory: " .. directory_path)
    end

    local files = fio.listdir(directory_path)
    for _, file in ipairs(files) do
        local file_path = fio.pathjoin(directory_path, file)
        local file_stat = fio.stat(file_path)
        if file_stat:is_dir() then
            -- Recursively deleting the subdirectory
            delete_directory(file_path)
        else
            -- Delete the file
            local success, err = fio.unlink(file_path)
            if not success then
                error("Failed to delete file: " .. to_string(file_path) .. " - " .. err)
            end
            print(string.format("File deleted: '%s'", file_path))
        end
    end

    -- Delete the directory
    local success, err = fio.rmdir(directory_path)
    if not success then
        error("Failed to delete directory: " .. to_string(directory_path) .. " - " .. err)
    end

    print(string.format("Directory '%s' successfully deleted", directory_path))
end


local function create_memtx()
    local memtx_path = './memtx_dir'

    if fio.path.exists(memtx_path) then
        fio.rmtree(memtx_path)
    end
    fio.mkdir(memtx_path)

    box.cfg {
        checkpoint_count = 2,
        memtx_use_mvcc_engine = true,
        memtx_dir = memtx_path,
        txn_isolation = 'best-effort'
    }
end

local function clear_dirs_for_all_replicas()
    local base_dir = fio.abspath('./replicas_dirs')
    if fio.path.exists(base_dir) then
        fio.rmtree(base_dir)
    end
end


--- Utils functions for debugging
local function ensure_replica_dirs_exist()
    local replica_dirs_path = fio.abspath('./replicas_dirs')

    if not fio.path.exists(replica_dirs_path) then
        local ok, err = fio.mkdir(replica_dirs_path)
        if not ok then
            error(string.format("Failed to create directory '%s': %s", replica_dirs_path, err))
        end
        print(string.format("Directory '%s' successfully created.", replica_dirs_path))
    elseif not fio.path.is_dir(replica_dirs_path) then
        error(string.format("Path '%s' exists but is not a directory", replica_dirs_path))
    end
end

local function create_dirs_for_replica(replica_id)
    ensure_replica_dirs_exist()
    local base_dir = fio.abspath(string.format('./replicas_dirs/replica_%d', replica_id))
    local memtx_dir = fio.pathjoin(base_dir, './memtx_dir')
    local wal_dir = fio.pathjoin(base_dir, './wal_dir')
    local log_dir = fio.pathjoin(base_dir, './log_dir')

    fio.rmtree(memtx_dir)
    fio.rmtree(wal_dir)
    fio.rmtree(log_dir)

    if not fio.path.exists(base_dir) then
        fio.mkdir(base_dir)
    end
    if not fio.path.exists(memtx_dir) then
        fio.mkdir(memtx_dir)
    end
    if not fio.path.exists(wal_dir) then
        fio.mkdir(wal_dir)
    end
    if not fio.path.exists(log_dir) then
        fio.mkdir(log_dir)
    end

    return memtx_dir, wal_dir, log_dir
end

return {
    copy_file = copy_file,
    delete_file = delete_file,
    delete_directory = delete_directory,
    create_memtx = create_memtx,
    clear_dirs_for_all_replicas = clear_dirs_for_all_replicas,
    create_dirs_for_replica = create_dirs_for_replica,
    add_error_scenario = add_error_scenario
}
