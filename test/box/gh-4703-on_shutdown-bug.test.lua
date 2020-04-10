env = require('test_run')
fio = require("fio")
test_run = env.new()

--
-- gh-4703: Make sure that on_shutdown triggers are executed after
-- EOF.
--
file_name = "on_shutdown_triggered.txt"
test_run:cmd("setopt delimiter ';'");
on_shutdown_cmd = "box.ctl.on_shutdown(function() local fio = require('fio') "..
                   "fio.open('"..file_name.."', "..
                   "{'O_CREAT', 'O_TRUNC', 'O_WRONLY'}, 777):close() end)";
test_run:cmd("setopt delimiter ''");
server = io.popen('tarantool -i', 'w')
server:write(on_shutdown_cmd)
server:close()
fio.path.lexists(file_name) == true
os.remove(file_name)
