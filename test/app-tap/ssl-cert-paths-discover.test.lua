#!/usr/bin/env tarantool

local tap = require("tap")
local ffi = require("ffi")
local fio = require("fio")

--
-- gh-5615: Static build: Bundled curl in Tarantool http client does
-- not accept a valid SSL certificate
--
local test = tap.test("ssl_cert_paths_discover")
test:plan(6)


----- Init test env

ffi.cdef([[
    const char* X509_get_default_cert_dir_env();
    const char* X509_get_default_cert_file_env();
    void ssl_cert_paths_discover(int overwrite);
    const char *default_cert_dir_paths[];
    const char *default_cert_file_paths[];
]])

local CERT_DIR_ENV = ffi.string(ffi.C.X509_get_default_cert_dir_env())
local CERT_FILE_ENV = ffi.string(ffi.C.X509_get_default_cert_file_env())

local temp_dir = fio.tempdir()
local cert_file1 = fio.pathjoin(temp_dir, "cert1.pem")
local cert_file2 = fio.pathjoin(temp_dir, "cert2.pem")

local handle = fio.open(cert_file1, {"O_RDWR", "O_CREAT"}, tonumber("755", 8))
handle:close()
fio.copyfile(cert_file1, cert_file2)


----- Helpers

local function cert_dir_paths_mockable()
    return pcall(function()
        return ffi.C.default_cert_dir_paths
    end)
end

local function cert_file_paths_mockable()
    return pcall(function()
        return ffi.C.default_cert_file_paths
    end)
end

local function mock_cert_paths_by_addr(addr, paths)
    -- insert NULL to the end of array as flag of end
    local paths = table.copy(paths)
    table.insert(paths, box.NULL)

    local mock_paths = ffi.new(("const char*[%s]"):format(#paths), paths)
    ffi.copy(addr, mock_paths, ffi.sizeof(mock_paths))
    ffi.C.ssl_cert_paths_discover(1)
end

local function mock_cert_dir_paths(t, dir_paths)
    local dir_paths_addr = ffi.C.default_cert_dir_paths;
    t:diag("Mock cert dir paths: %s", table.concat(dir_paths, ";"))
    mock_cert_paths_by_addr(dir_paths_addr, dir_paths)
end

local function mock_cert_file_paths(t, file_paths)
    local file_paths_addr = ffi.C.default_cert_file_paths
    t:diag("Mock cert file paths: %s", table.concat(file_paths, ";"))
    mock_cert_paths_by_addr(file_paths_addr, file_paths)
end

local function run_user_defined_env(t, var_name, var_val)
    local binary_dir = arg[-1]
    local log_file = fio.pathjoin(temp_dir, "out.log")
    local cmd = string.format(
        [[%s=%s %s -e 'print(os.getenv(%q)) os.exit(0)' 1>%s 2>&1]],
        var_name, var_val, binary_dir, var_name, log_file
    )

    t:diag("Run cmd: %s", cmd)

    local status = os.execute(cmd)
    local output = io.open(log_file, "r"):read("*a"):strip()

    t:is(status, 0, "exit status 0")
    return output and output:strip()
end


----- Tests

if not cert_dir_paths_mockable() then
    -- Because of LTO (especially on macOS) symbol default_cert_dir_paths
    -- may become local and unavailable through ffi, so there is no
    -- chance to mock tests
    test:skip("Default cert dir paths would set")
    test:skip("Invalid dir paths won't set")
else
    test:test("Default cert dir paths would set", function(t)
        t:plan(2)

        mock_cert_dir_paths(t, {temp_dir})
        t:is(
            os.getenv(CERT_DIR_ENV), temp_dir, "One cert dir path was set"
        )

        local dir_paths = {temp_dir, temp_dir}
        mock_cert_dir_paths(t, dir_paths)
        t:is(
            os.getenv(CERT_DIR_ENV),
            table.concat(dir_paths, ":"),
            "Multiple cert dir paths (like unix $PATH) was set"
        )
    end)

    test:test("Invalid dir paths won't set", function(t)
        t:plan(2)

        -- Cleanup env
        os.setenv(CERT_DIR_ENV, nil)

        mock_cert_dir_paths(t, {"/not/existing_dir"})
        t:is(os.getenv(CERT_DIR_ENV), nil, "Not existing cert dir wasn't set")

        local empty_dir_name = fio.pathjoin(temp_dir, "empty")
        fio.mkdir(empty_dir_name)

        mock_cert_dir_paths(t, {empty_dir_name})
        t:is(os.getenv(CERT_DIR_ENV), nil, "Empty cert dir wasn't set")
    end)
end


if not cert_file_paths_mockable() then
    -- Because of LTO (especially on macOS) symbol default_cert_file_paths
    -- may become local and unavailable through ffi, so there is no
    -- chance to mock tests
    test:skip("Default cert file path would set")
    test:skip("Invalid cert file won't set")
else
    test:test("Default cert file path would set", function(t)
        t:plan(2)

        mock_cert_file_paths(t, {cert_file1})
        t:is(os.getenv(CERT_FILE_ENV), cert_file1, "Cert file was set")

        mock_cert_file_paths(t, {cert_file2, cert_file1})
        t:is(os.getenv(CERT_FILE_ENV), cert_file2, "Only one (first) existing default cert file was set")
    end)

    test:test("Invalid cert file won't set", function(t)
        t:plan(1)

        -- Cleanup
        os.setenv(CERT_FILE_ENV, nil)

        mock_cert_file_paths(t, {"/not/existing_dir/cert1"})
        t:is(os.getenv(CERT_FILE_ENV), nil, "Not existing cert file wasn't set")
    end)
end

test:test("User defined cert dir won't be overridden", function(t)
    t:plan(2)

    local res = run_user_defined_env(t, CERT_DIR_ENV, "/dev/null")
    t:is(res, "/dev/null", "User defined wasn't overridden")
end)


test:test("User defined cert file won't be overridden", function(t)
    t:plan(2)

    local res = run_user_defined_env(t, CERT_FILE_ENV, "/dev/null/cert.pem")
    t:is(res, "/dev/null/cert.pem", "User defined wasn't overridden")
end)

----- Cleanup
fio.rmtree(temp_dir)

os.exit(test:check() and 0 or 1)
