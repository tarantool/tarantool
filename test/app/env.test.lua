env = require('env')
trun = require('test_run').new()

env['HELL'] = '123'
do return env['HELL'] end
os.getenv('HELL')
env['HELL'] = nil
do return env['HELL'] end
os.getenv('HELL')

env_dict = env()
type(env_dict)
trun:cmd("setopt delimiter ';'")
do
    for k, v in pairs(env_dict) do
        if type(k) ~= 'string' or type(v) ~= 'string' then
            return false
        end
    end
    return true
end;
trun:cmd("setopt delimiter ''")
