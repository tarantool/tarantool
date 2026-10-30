--
-- gh-5968: Test that compiled C function can be restored to the former
-- values when a new module can't be loaded for some reason (say there are
-- missing functions).
--
build_path = os.getenv("BUILDDIR")
old_cpath = package.cpath
package.cpath = build_path..'/test/box/?.so;'..build_path..'/test/box/?.dylib;'..package.cpath

fio = require('fio')

ext = (jit.os == "OSX" and ".dylib" or ".so")

path_func_restore = fio.pathjoin(build_path, "test/box/func_restore") .. ext
path_func_good = fio.pathjoin(build_path, "test/box/func_restore1") .. ext

_ = pcall(fio.unlink(path_func_restore))
fio.symlink(path_func_good, path_func_restore)

box.schema.func.create('func_restore.echo_1', {language = "C"})
box.schema.func.create('func_restore.echo_2', {language = "C"})
box.schema.func.create('func_restore.echo_3', {language = "C"})

box.schema.user.grant('guest', 'execute', 'function', 'func_restore.echo_3')
box.schema.user.grant('guest', 'execute', 'function', 'func_restore.echo_2')
box.schema.user.grant('guest', 'execute', 'function', 'func_restore.echo_1')

assert(box.func['func_restore.echo_3']:call() == 3)
assert(box.func['func_restore.echo_2']:call() == 2)
assert(box.func['func_restore.echo_1']:call() == 1)

function run_restore(path)                                      \
    pcall(fio.unlink(path_func_restore))                        \
    fio.symlink(path, path_func_restore)                        \
                                                                \
    local ok, _ = pcall(box.schema.func.reload, "func_restore") \
    assert(not ok)                                              \
                                                                \
    assert(box.func['func_restore.echo_1']:call() == 1)         \
    assert(box.func['func_restore.echo_2']:call() == 2)         \
    assert(box.func['func_restore.echo_3']:call() == 3)         \
end

bad_modules = {                                                 \
    fio.pathjoin(build_path, "test/box/func_restore2") .. ext,  \
    fio.pathjoin(build_path, "test/box/func_restore3") .. ext,  \
    fio.pathjoin(build_path, "test/box/func_restore4") .. ext,  \
}

for k, v in ipairs(bad_modules) do run_restore(v) end

box.schema.func.drop('func_restore.echo_1')
box.schema.func.drop('func_restore.echo_2')
box.schema.func.drop('func_restore.echo_3')

package.cpath = old_cpath
