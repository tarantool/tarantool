
arg[0] ~= nil
arg[1] ~= nil
string.match(arg[0], '^/') ~= nil
string.match(arg[1], '^/') ~= nil

string.match(arg[0], '/tarantool$') ~= nil
string.match(arg[1], '/box%.lua$') ~= nil

io.type( io.open(arg[0]) )
io.type( io.open(arg[1]) )

