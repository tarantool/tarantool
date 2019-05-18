test_run = require('test_run').new()
buffer = require('buffer')
ffi = require('ffi')

-- Alignment.
_ = buffer.static_alloc('char') -- This makes buffer pos unaligned.
p = buffer.static_alloc('int')
ffi.cast('int', p) % ffi.alignof('int') == 0 -- But next alloc is aligned.

-- Able to allocate bigger than static buffer - such allocations
-- are on the heap.
type(buffer.static_alloc('char', 13000))
