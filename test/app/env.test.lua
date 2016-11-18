os = require('os')
test_run = require('test_run').new()

os.setenv('location', 'Hell_Hotel')
os.getenv('location')
os.setenv('location', nil)

do os.getenv('location') end

env_dict = os.environ()
type(env_dict)
test_run:cmd("setopt delimiter ';'")
do
    for k, v in pairs(env_dict) do
        if type(k) ~= 'string' or type(v) ~= 'string' then
            return false
        end
    end
    return true
end;
test_run:cmd("setopt delimiter ''")
