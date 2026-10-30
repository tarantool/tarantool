--
-- gh-4642: New box.lib module to be able to
-- run C stored functions on read only nodes
-- without requirement to register them with
-- box.schema.func help.
--
build_path = os.getenv("BUILDDIR")
package.cpath = build_path..'/test/box/?.so;'..build_path..'/test/box/?.dylib;'..package.cpath

fio = require('fio')

ext = (jit.os == "OSX" and "dylib" or "so")

cfunc_path = fio.pathjoin(build_path, "test/box/cfunc.") .. ext
cfunc1_path = fio.pathjoin(build_path, "test/box/cfunc1.") .. ext
cfunc2_path = fio.pathjoin(build_path, "test/box/cfunc2.") .. ext
cfunc3_path = fio.pathjoin(build_path, "test/box/cfunc3.") .. ext
cfunc4_path = fio.pathjoin(build_path, "test/box/cfunc4.") .. ext

_ = pcall(fio.unlink(cfunc_path))
fio.symlink(cfunc1_path, cfunc_path)

_, err = pcall(box.lib.load, 'non-such-module')
assert(err ~= nil)

-- All functions are sitting in cfunc.so.
old_module = box.lib.load('cfunc')
assert(old_module['debug_refs'] == 1)
old_module_copy = box.lib.load('cfunc')
assert(old_module['debug_refs'] == 2)
assert(old_module_copy['debug_refs'] == 2)
old_module_copy:unload()
assert(old_module['debug_refs'] == 1)
old_cfunc_nop = old_module:load('cfunc_nop')
old_cfunc_echo = old_module:load('cfunc_echo')
old_cfunc_sum = old_module:load('cfunc_sum')
assert(old_module['debug_refs'] == 4)

-- Test for error on nonexisting function.
_, err = pcall(old_module.load, old_module, 'no-such-func')
assert(err ~= nil)

-- Make sure they all are callable.
old_cfunc_nop()
old_cfunc_echo()
old_cfunc_sum()

-- Unload the module but keep old functions alive, so
-- they keep reference to NOP module internally
-- and still callable.
old_module:unload()
-- Test refs via function name.
assert(old_cfunc_nop['debug_module_refs'] == 3)
old_cfunc_nop()
old_cfunc_echo()
old_cfunc_sum()

-- The module is unloaded I should not be able
-- to load new shared library.
old_module:load('cfunc')
-- Neither I should be able to unload module twise.
old_module:unload()

-- Clean old functions.
old_cfunc_nop:unload()
old_cfunc_echo:unload()
assert(old_cfunc_sum['debug_module_refs'] == 1)
old_cfunc_sum:unload()

-- And reload old module again.
old_module = box.lib.load('cfunc')
old_module_ptr = old_module['debug_ptr']
assert(old_module['debug_refs'] == 1)

-- Overwrite module with new contents.
_ = pcall(fio.unlink(cfunc_path))
fio.symlink(cfunc2_path, cfunc_path)

-- Load new module, cache should be updated.
new_module = box.lib.load('cfunc')
new_module_ptr = new_module['debug_ptr']

-- Old and new module keep one reference with
-- different IDs.
assert(old_module['debug_refs'] == 1)
assert(old_module['debug_refs'] == new_module['debug_refs'])
assert(old_module_ptr ~= new_module_ptr)

-- All functions from old module should be loadable.
old_cfunc_nop = old_module:load('cfunc_nop')
old_cfunc_echo = old_module:load('cfunc_echo')
old_cfunc_sum = old_module:load('cfunc_sum')
assert(old_cfunc_nop['debug_module_ptr'] == old_module_ptr)
assert(old_cfunc_echo['debug_module_ptr'] == old_module_ptr)
assert(old_cfunc_sum['debug_module_ptr'] == old_module_ptr)
assert(old_module['debug_refs'] == 4)

-- Lookup for updated symbols.
new_cfunc_nop = new_module:load('cfunc_nop')
new_cfunc_echo = new_module:load('cfunc_echo')
new_cfunc_sum = new_module:load('cfunc_sum')
assert(new_cfunc_nop['debug_module_ptr'] == new_module_ptr)
assert(new_cfunc_echo['debug_module_ptr'] == new_module_ptr)
assert(new_cfunc_sum['debug_module_ptr'] == new_module_ptr)
assert(new_module['debug_refs'] == 4)

-- Call old functions.
old_cfunc_nop()
old_cfunc_echo()
old_cfunc_sum()

-- Call new functions.
new_cfunc_nop()
new_cfunc_echo({1, 2, 3})
new_cfunc_echo(1, 2, 3)
new_cfunc_sum(1) -- error, one arg passed
new_cfunc_sum(1, 2)

-- Cleanup old module's functions.
old_cfunc_nop:unload()
old_cfunc_echo:unload()
old_cfunc_sum:unload()
old_module:unload()

-- Cleanup new module data.
new_cfunc_nop:unload()
new_cfunc_echo:unload()
new_cfunc_sum:unload()
new_module:unload()

-- Cleanup the generated symlink.
_ = pcall(fio.unlink(cfunc_path))

-- Test double hashing: create function
-- in box.schema.fun so that it should
-- appear in box.lib hash.
fio.symlink(cfunc3_path, cfunc_path)
box.schema.func.create('cfunc.cfunc_add', {language = "C"})
box.func['cfunc.cfunc_add']:call({1, 2})

-- Now we have 2 refs for low level module (when function
-- is loaded for first time from box.schema.func it takes
-- two refs: one for module itself and one for for the function,
-- this is because there is no explicit "load" procedure for
-- box.schema.func, next loads from the same module via box.schema.func
-- interface grabs the module from cache and add only one reference).
--
-- So we have 2 refs for box.schema.func and one for box.lib
-- interface which grabs the same module.
old_module = box.lib.load('cfunc')
assert(old_module['debug_refs'] == 3)
old_func = old_module:load('cfunc_add')
assert(old_module['debug_refs'] == 4) -- plus function instance
old_func(1, 2)

-- Now update on disk and reload the module.
_ = pcall(fio.unlink(cfunc_path))
fio.symlink(cfunc4_path, cfunc_path)

box.schema.func.reload("cfunc")
box.func['cfunc.cfunc_add']:call({1, 2})

-- The box.lib instance should carry own references to
-- the module and old function. And reloading must not
-- affect old functions. Thus one for box.lib _module_ and
-- one for box.lib _function_. The reloaded box.schema.func
-- will carry own two references for reloaded module and
-- bound function.
assert(old_module['debug_refs'] == 2)
old_func(1, 2)
old_func:unload()
old_module:unload()

-- Same time the reload should update low level module cache,
-- thus we load a new instance from updated cache entry which
-- has 2 references already and thus we add one more reference.
new_module = box.lib.load('cfunc')
assert(new_module['debug_refs'] == 3)

-- Box function should carry own module.
box.func['cfunc.cfunc_add']:call({1, 2})

new_module:unload()
box.schema.func.drop('cfunc.cfunc_add')

-- Now lets try to figure out if __gc works as expected.
module1 = box.lib.load('cfunc')
module2 = box.lib.load('cfunc')
assert(module1['debug_module_ptr'] == module2['debug_module_ptr'])
assert(module1['debug_refs'] == 2)
cfunc_add = module2:load('cfunc_add')
assert(module1['debug_refs'] == 3)
module2 = nil
collectgarbage('collect')
assert(module1['debug_refs'] == 2)
cfunc_add = nil
collectgarbage('collect')
assert(module1['debug_refs'] == 1)
module1:unload()

-- Cleanup.
_ = pcall(fio.unlink(cfunc_path))
