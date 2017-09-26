fio = require('fio')
env = require('test_run')
test_run = env.new()
path_app = os.getenv("SOURCEDIR") .. "/test/app/"
fio.chdir(path_app)
f = package.loaders[2]("lua.fiber")
type(f)

tmp_name = fio.tempdir()
dir_name = tmp_name .. "/pr"
rocks_dir = "/.rocks/share/tarantool";

test_run:cmd("setopt delimiter ';'");
function create_dirs(name)
    fio.mkdir(name)
    fio.mkdir(name .. "/.rocks")
    fio.mkdir(name .. "/.rocks/share")
    fio.mkdir(name .. "/.rocks/lib")
    fio.mkdir(name .. "/.rocks/share/tarantool")
    fio.mkdir(name .. "/.rocks/lib/tarantool")
end;
test_run:cmd("setopt delimiter ''");

create_dirs(dir_name)
create_dirs(dir_name .. "/pr1/")
create_dirs(dir_name .. "/pr1/pr2/")

os.execute("cp lua/fiber.lua " .. dir_name .. rocks_dir)
os.execute("cp lua/fiber.lua " .. dir_name .. "/pr1/pr2/" .. rocks_dir)
os.execute("cp " .. path_app .. "../app-tap/module_api.so " .. dir_name .. "/pr1/pr2/.rocks/lib/tarantool")
fio.chdir(dir_name .. "/pr1/pr2/")
fio.rename(dir_name .. "/pr1/pr2/" .. rocks_dir .. "/fiber.lua", dir_name .. "/pr1/pr2/".. rocks_dir .."/fiber1.lua")

f = package.loaders[3]("fiber")
type(f)
f = package.loaders[3]("fiber1")
type(f)
f = package.loaders[3]("module_api")
type(f)

fio.chdir(dir_name .. "/pr1/")
-- error as it lies in child dir
f = package.loaders[3]("fiber")
type(f)
