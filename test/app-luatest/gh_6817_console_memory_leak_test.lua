local console = require('console')
local ffi = require('ffi')
local fiber = require('fiber')
local t = require('luatest')
local g = t.group()

ffi.cdef([[
    int pipe(int pipefd[2]);
    int dup2(int oldfd, int newfd);
    ssize_t write(int fd, const void *buf, size_t count);
    int close(int fd);
]])

g.before_test('test_console_mem_leak', function()
    -- Replace stdin fd with a pipe fd so that we can emulate user input.
    local fd = ffi.new([[ int[2] ]])
    assert(ffi.C.pipe(fd) == 0)
    if fd[0] ~= 0 then
        assert(ffi.C.dup2(fd[0], 0) == 0)
        assert(ffi.C.close(fd[0]) == 0)
    end
    g.console_write_fd = fd[1]
    g.console_write = function(command)
        ffi.C.write(g.console_write_fd, command, string.len(command))
    end
end)

g.after_test('test_console_mem_leak', function()
    assert(ffi.C.close(g.console_write_fd) == 0)
    assert(ffi.C.close(0) == 0)
    g.console_write = nil
    g.console_write_fd = nil
end)

-- Checks that ASAN doesn't detect any memory leaks when console is used.
g.test_console_mem_leak = function()
    local on_stop = fiber.channel()
    fiber.create(function()
        console.start()
        on_stop:put(true)
    end)
    g.console_write('\\help\n')
    g.console_write('\\quit\n')
    t.assert(on_stop:get(10))
end
