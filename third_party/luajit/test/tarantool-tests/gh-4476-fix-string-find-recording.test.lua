local tap = require('tap')
local test = tap.test('gh-4476-fix-string-find-recording'):skipcond({
  ['Test requires JIT enabled'] = not jit.status(),
})

test:plan(1)

local err = [[module 'kit.1.10.3-136' not found:
	no field package.preload['kit.1.10.3-136']
	no file './kit/1/10/3-136.lua'
	no file './kit/1/10/3-136/init.lua'
	no file './kit/1/10/3-136.so'
	no file '/bug/.rocks/share/tarantool/kit/1/10/3-136.lua'
	no file '/bug/.rocks/share/tarantool/kit/1/10/3-136/init.lua'
	no file '/.rocks/share/tarantool/kit/1/10/3-136.lua'
	no file '/.rocks/share/tarantool/kit/1/10/3-136/init.lua'
	no file '/bug/.rocks/lib/tarantool/kit/1/10/3-136.so'
	no file '/.rocks/lib/tarantool/kit/1/10/3-136.so'
	no file '/bug/app/kit/1/10/3-136.lua'
	no file '/bug/app/kit/1/10/3-136/init.lua'
	no file '/bug/libs/share/lua/5.1/kit/1/10/3-136.lua'
	no file '/bug/libs/share/lua/5.1/kit/1/10/3-136/init.lua'
	no file '/root/.luarocks/share/lua/5.1/kit/1/10/3-136.lua'
	no file '/root/.luarocks/share/lua/5.1/kit/1/10/3-136/init.lua'
	no file '/root/.luarocks/share/lua/kit/1/10/3-136.lua'
	no file '/root/.luarocks/share/lua/kit/1/10/3-136/init.lua'
	no file '/usr/local/share/tarantool/kit/1/10/3-136.lua'
	no file '/usr/local/share/tarantool/kit/1/10/3-136/init.lua'
	no file '/usr/share/tarantool/kit/1/10/3-136.lua'
	no file '/usr/share/tarantool/kit/1/10/3-136/init.lua'
	no file '/usr/local/share/lua/5.1/kit/1/10/3-136.lua'
	no file '/usr/local/share/lua/5.1/kit/1/10/3-136/init.lua'
	no file '/usr/share/lua/5.1/kit/1/10/3-136.lua'
	no file '/usr/share/lua/5.1/kit/1/10/3-136/init.lua'
	no file '/bug/libs/lib/lua/5.1/kit/1/10/3-136.so'
	no file '/bug/libs/lib/lua/kit/1/10/3-136.so'
	no file '/bug/libs/lib64/lua/5.1/kit/1/10/3-136.so'
	no file '/root/.luarocks/lib/lua/5.1/kit/1/10/3-136.so'
	no file '/root/.luarocks/lib/lua/kit/1/10/3-136.so'
	no file '/usr/local/lib64/tarantool/kit/1/10/3-136.so'
	no file '/usr/lib64/tarantool/kit/1/10/3-136.so'
	no file '/usr/local/lib64/lua/5.1/kit/1/10/3-136.so'
	no file '/usr/lib64/lua/5.1/kit/1/10/3-136.so'
	no file '/bug/libs/lib/lua/5.1/kit.so'
	no file '/bug/libs/lib/lua/kit.so'
	no file '/bug/libs/lib64/lua/5.1/kit.so'
	no file '/root/.luarocks/lib/lua/5.1/kit.so'
	no file '/root/.luarocks/lib/lua/kit.so'
	no file '/usr/local/lib64/tarantool/kit.so'
	no file '/usr/lib64/tarantool/kit.so'
	no file '/usr/local/lib64/lua/5.1/kit.so'
	no file '/usr/lib64/lua/5.1/kit.so']]

local at, _, e
local count_vm = 0

jit.off()

repeat
  _, e = err:find("\n\t", at, true)
  at = e
  count_vm = count_vm + 1
until not e

local count_jit = 0

jit.on()
jit.opt.start(0, 'hotloop=1')

repeat
  _, e = err:find("\n\t", at, true)
  at = e
  count_jit = count_jit + 1
  assert(count_jit <= count_vm, "Trace goes in cycles")
until not e

test:is(count_vm, count_jit)

test:done(true)
