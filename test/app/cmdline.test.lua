arg[-1] ~= nil
arg[0] ~= nil
string.match(arg[-1], '^/') ~= nil
string.match(arg[0], '^/') == nil

string.match(arg[-1], '/tarantool$') ~= nil
string.match(arg[2], 'app%.lua$') ~= nil

io.type( io.open(arg[-1]) )
io.type( io.open(arg[0]) )

