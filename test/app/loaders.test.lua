fio = require('fio')
env = require('test_run')
test_run = env.new()
source_dir = os.getenv("SOURCEDIR") .. "/test/app/"
build_dir = os.getenv("BUILDDIR") .. "/test/app/"

--
-- Check . loader
--
orig_cwd = fio.cwd()
fio.chdir(source_dir)
cwd_loader = package.loaders[2]
f = cwd_loader("loaders")
type(f)
f()
fio.chdir(orig_cwd)

--
-- Check .rocks loader
--
tmp_dir = fio.tempdir()
work_dir = fio.pathjoin(tmp_dir, "pr")
fio.mkdir(work_dir)
pr1_dir = fio.pathjoin(work_dir, "pr1")
fio.mkdir(pr1_dir)
pr2_dir = fio.pathjoin(pr1_dir, "pr2")
fio.mkdir(pr2_dir)

lua_dir = ".rocks/share/tarantool"
lib_dir = ".rocks/lib/tarantool"

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

create_dirs(work_dir)
create_dirs(pr1_dir)
create_dirs(pr2_dir)

soext = (jit.os == "OSX" and "dylib" or "so")
loaders_path = fio.pathjoin(source_dir, "loaders.lua")
loaderslib_path = fio.pathjoin(build_dir, "loaderslib."..soext)

fio.symlink(loaders_path, fio.pathjoin(work_dir, lua_dir, "loaders.lua"))
fio.symlink(loaderslib_path, fio.pathjoin(pr1_dir, lib_dir, "loaderslib."..soext))

orig_cwd = fio.cwd()

fio.chdir(pr2_dir)
rocks_loader = package.loaders[4]
rocks_loader_dyn = package.loaders[5]
f = rocks_loader("loaders")
type(f)
f()
f = rocks_loader_dyn("loaderslib")
type(f)
f()
f = rocks_loader("loaders1")
type(f) -- error
package.loaded.loaders = nil
package.loaded.loaders1 = nil
package.loaded.loaderslib = nil

fio.chdir(work_dir)
f = rocks_loader("loaders")
type(f)
f()
f = rocks_loader("loaders1")
type(f) -- error
f = rocks_loader_dyn("loaderslib")
type(f) -- error

fio.chdir(orig_cwd)
