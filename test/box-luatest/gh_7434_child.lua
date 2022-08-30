local fiber = require('fiber')

local file_name = arg[1]
local test_mode = arg[2]
assert(file_name)

-- Create a file to check that shutdown trigger continues execution after yield
box.ctl.on_shutdown(
    function()
        fiber.sleep(0)

        local fio = require('fio')
        local f = fio.open(file_name, {'O_CREAT', 'O_TRUNC', 'O_WRONLY'},
                           tonumber('644', 8))
        f:close()
    end
)

-- Unlock ph:read() in the parent process
print('started')
io.flush()

if test_mode == 'background' then
    fiber.create(
        function()
            while true do
                fiber.sleep(0.001)
            end
    end)
end

if test_mode == 'sleep' then
    fiber.sleep(1000)
end

if test_mode == 'os_exit' then
    os.exit(42)
end
