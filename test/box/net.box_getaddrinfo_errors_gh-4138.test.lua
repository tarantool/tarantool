env = require('test_run')
test_run = env.new()
remote = require('net.box')

-- gh-4138 Check getaddrinfo() error from connect() only. Error
-- code and error message returned by getaddrinfo() depends on
-- system's gai_strerror().
test_run:cmd("setopt delimiter ';'")
function check_err(err)
    if err:startswith('getaddrinfo') then
        return true
    end
    return false
end;
test_run:cmd("setopt delimiter ''");

s = remote.connect('non_exists_hostname:3301')
check_err(s['error'])
