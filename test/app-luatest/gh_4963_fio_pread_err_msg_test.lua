local fio = require('fio')
local t = require('luatest')

local g = t.group()

g.test_fio_pread_err_msg = function()
    local fh = fio.open('/dev/null', {'O_RDONLY'})

    t.assert_error_msg_contains("Usage: fh:pread(buf, size[, offset]) or fh:pread(size[, offset])", fh.pread, fh)
end
