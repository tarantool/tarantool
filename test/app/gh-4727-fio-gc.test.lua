test_run = require('test_run').new()
fiber = require('fiber')
fio = require('fio')
--
-- gh-4727: fio handler GC.
--
flags = {'O_CREAT', 'O_RDWR'}
mode = {'S_IRWXU'}
filename = 'test4727.txt'
fh1 = nil
fh2 = nil
-- Idea of the test is that according to the Open Group standard,
-- open() always returns the smallest available descriptor. This
-- means, that in 'open() + close() + open()' the second open()
-- should return the same value as the first call, if no other
-- threads/fibers managed to interfere. Because of the
-- interference the sequence may need to be called multiple times
-- to catch a couple of equal descriptors.
test_run:wait_cond(function()                                       \
    collectgarbage('collect')                                       \
    local f = fio.open(filename, flags, mode)                       \
    fh1 = f.fh                                                      \
    f = nil                                                         \
    collectgarbage('collect')                                       \
-- GC function of a fio object works in a separate fiber. Give it   \
-- time to execute.                                                 \
    fiber.yield()                                                   \
    f = fio.open(filename, flags, mode)                             \
    fh2 = f.fh                                                      \
    f = nil                                                         \
    collectgarbage('collect')                                       \
    fiber.yield()                                                   \
    return fh1 == fh2                                               \
end) or {fh1, fh2}

-- Ensure, that GC does not break anything after explicit close.
-- Idea of the test is the same as in the previous test, but now
-- the second descriptor is used for something. If GC of the first
-- fio object is called even after close(), it would close the
-- same descriptor, already used by the second fio object. And it
-- won't be able to write anything. Or will write, but to a
-- totally different descriptor created by some other
-- fiber/thread. This is why read() is called on the same file
-- afterwards.
f = nil
test_run:wait_cond(function()                                       \
    f = fio.open(filename, flags, mode)                             \
    fh1 = f.fh                                                      \
    f:close()                                                       \
    f = fio.open(filename, flags, mode)                             \
    fh2 = f.fh                                                      \
    return fh1 == fh2                                               \
end)
collectgarbage('collect')
fiber.yield()
f:write('test')
f:close()
f = fio.open(filename, flags, mode)
f:read()
f:close()
