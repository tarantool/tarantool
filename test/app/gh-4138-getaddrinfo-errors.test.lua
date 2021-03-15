env = require('test_run')
test_run = env.new()
socket = require('socket')

-- gh-4138 Check getaddrinfo() error from socket:connect() only.
-- Error code and error message returned by getaddrinfo() depends
-- on system's gai_strerror().
test_run:cmd("setopt delimiter ';'")
function check_err(err)
    if err:startswith('getaddrinfo') then
        return true
    end
    return false
end;
test_run:cmd("setopt delimiter ''");

s, err = socket.getaddrinfo('non_exists_hostname', 3301)
check_err(err)
s, err = socket.connect('non_exists_hostname', 3301)
check_err(err)
s, err = socket.tcp_connect('non_exists_hostname', 3301)
check_err(err)
s, err = socket.bind('non_exists_hostname', 3301)
check_err(err)
s, err = socket.tcp_server('non_exists_hostname', 3301, function() end)
check_err(err)
