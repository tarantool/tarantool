local fio = require('fio')

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



return {
    copy_file = copy_file,
    delete_file = delete_file,
    delete_directory = delete_directory
}
