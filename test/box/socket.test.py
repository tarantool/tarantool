# encoding: utf-8

import socket

########################
#                      #
# box.socket.tcp/udp() #
#                      #
########################

admin("s = box.socket.udp()")
admin("type(s)")
admin("s:close()")
admin("s = box.socket.tcp()")
admin("type(s)")

### socket:close()

# close
admin("s:close()")

# close (on closed)
admin("s:close()")

# error
admin("s:error()")

####################
#                  #
# socket:connect() #
#                  #
####################

admin("s:connect('localhost', '30303')")
# Connection refused
admin("s:error()")

admin("s:connect('127.0.0.1', '30303')")
# Connection refused
admin("s:error()")

admin("s:connect('127.0.0.1', '30303', 0.01)")
# connection refused
admin("s:error()")

# bad args
admin("s:connect('127.0.0.1')")
admin("s:connect()")
admin("s:connect(123)")
admin("s:close()")

admin("s:close()")
admin("sr, se = s:connect('somewhereelse', '30303', 0.0001)")
admin("sr == nil and se == 'error' or se == 'timeout'")

# timedout or hostname resolution failed
admin("e = s:error()")
admin("e == -1 or e == 110")
admin("s:close()")

#################
#               #
# socket:send() #
#               #
#################

# bad args
admin("s:send()")
admin("s:send(1)")

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('localhost', 30303))
s.listen(1)

admin("s = box.socket.tcp()")
admin("type(s:connect('127.0.0.1', '30303'))")
admin("s:send('ping')")
admin("s:error()")

# timedout - try to send 64MB (8388608 * 8 bytes) in 0.0000001 sec
admin("n, status, error_code, error_str = s:send(string.rep('=', 8388608), 0.0000001)")
admin("type(n)")
admin("type(status)")
admin("type(error_code)")
admin("type(error_str)")
admin("status")
admin("error_code")
admin("error_str")

admin("s:error()")

conn, addr = s.accept()
print('connected')

conn.close()
s.close()

# connection reset by peer
admin("s:send('ping')")
admin("s:error()")

admin("s:close()")

#################
#               #
# socket:recv() #
#               #
#################

# bad args
admin("s:recv()")

# test for case #1: successful recv
#
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('localhost', 30308))
s.listen(1)
admin("type(s:connect('127.0.0.1', '30308'))")
admin("s:error()")
conn, addr = s.accept()
print(conn.send("Hello, World"))
admin("s:recv(12)")
admin("s:error()")
admin("s:close()")
conn.close()
s.close()

# test for case #1: successful serial chunk recv (3 x chunk)
#
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('localhost', 30308))
s.listen(1)
admin("type(s:connect('127.0.0.1', '30308'))")
admin("s:error()")
conn, addr = s.accept()
print(conn.send("Hello World Oversized"))
admin("s:recv(11)")
admin("s:recv(5)")
admin("s:recv(5)")
admin("s:error()")
admin("s:close()")
conn.close()
s.close()

# test for case #2-3: timedout, error
#
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('localhost', 30308))
s.listen(1)

admin("type(s:connect('127.0.0.1', '30308'))")
admin("s:error()")

conn, addr = s.accept()
print('connected')

print(conn.send("pin"))
admin("s:recv(4, 0.01)")
# timedout
admin("s:error()")

conn.send("g")

# ping
admin("s:recv(4)")
admin("s:error()")

admin("s:close()")
conn.close()
s.close()

# test for case #4: EOF (data is less then recv size)
#
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('localhost', 30309))
s.listen(1)

admin("type(s:connect('127.0.0.1', '30309'))")
admin("s:error()")

conn, addr = s.accept()
print('connected')

print(conn.send("ping"))
conn.close()
admin("s:recv(6)")
admin("s:error()")
admin("s:close()")
s.close()

# test for case #4: EOF (data is more then recv size)
#
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('localhost', 30311))
s.listen(1)

admin("type(s:connect('127.0.0.1', '30311'))")
admin("s:error()")

conn, addr = s.accept()
print('connected')

print(conn.send("ping ping ping ping end "))
conn.close()

# recv should not say EOF, even if it really happened
admin("s:recv(5)")
admin("s:recv(5)")
admin("s:recv(5)")
admin("s:recv(5)")

# eof
admin("s:recv(5)")
# eof (zero)
admin("s:recv(5)")
# eof (zero)
admin("s:recv(5)")

admin("s:error()")
admin("s:close()")
s.close()

#####################
#                   #
# socket:readline() #
#                   #
#####################

# possible usage:
#
# 1. readline() == readline(limit == inf, seplist == {'\n'}, timeout == inf)
# 2. readline(limit)
# 3. readline(limit, timeout)
# 4. readline({seplist})
# 5. readline(limit, {seplist})
# 6. readline({seplist}, timeout)
# 7. readline(limit, {seplist}, timeout)

# test for case #1, 4: separator or limit found + different usage
#
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('localhost', 30305))
s.listen(1)
admin("type(s:connect('127.0.0.1', '30305'))")
admin("s:error()")
conn, addr = s.accept()

# 1. readline() == readline(limit == inf, seplist == {'\n'}, timeout == inf)
print(conn.send("Hello World\n"))
admin("s:readline()")
admin("s:error()")

# 2. readline(limit)
print(conn.send("Hello World\n"))
admin("s:readline(5)")
admin("s:error()")

# 3. readline(limit, timeout) (just api check)
admin("s:readline(5, 0.01)")
admin("s:error()")
admin("s:readline(6, 0.01)")
admin("s:error()")

# 4. readline({seplist})
print(conn.send("AbcDefGhi"))
admin("s:readline({'i', 'D'})")
admin("s:error()")
admin("s:readline({'i', 'G'})")
admin("s:error()")
admin("s:readline({'i'})")
admin("s:error()")

print(conn.send("CatCowDogStar"))
admin("s:readline({'Cat', 'Cow', 'Dog', 'Star'})")
admin("s:error()")
admin("s:readline({'Cat', 'Cow', 'Dog', 'Star'})")
admin("s:error()")
admin("s:readline({'Cat', 'Cow', 'Dog', 'Star'})")
admin("s:error()")
admin("s:readline({'Cat', 'Cow', 'Dog', 'Star'})")
admin("s:error()")

# 5. readline(limit, {seplist})
print(conn.send("CatCoowDoggStar"))
admin("s:readline(3, {'Cat', 'Coow'})")
admin("s:error()")
admin("s:readline(3, {'Cat', 'Coow'})")
admin("s:error()")
admin("s:readline(3, {'Dogg', 'Star'})")
admin("s:error()")
admin("s:readline(3, {'Dogg', 'Star'})")
admin("s:error()")

# read 'tar' part
admin("s:readline(3)")
admin("s:error()")

# 6. readline({seplist}, timeout)
print(conn.send("KKongKingCezaCezarCrown"))
admin("sl = {'Crown', 'King', 'Kong', 'Cezar'}")
admin("s:readline(sl, 1.0)")
admin("s:error()")
admin("s:readline(sl, 1.0)")
admin("s:error()")
admin("s:readline(sl, 1.0)")
admin("s:error()")
admin("s:readline(sl, 1.0)")
admin("s:error()")

# 7. readline(limit, {seplist}, timeout)
print(conn.send("RoAgathaPopPoCornDriveRoad"))
admin("sl = {'Agatha', 'Road', 'Corn', 'Drive', 'Pop'}")
admin("s:readline(64, sl, 1.0)")
admin("s:error()")
admin("s:readline(64, sl, 1.0)")
admin("s:error()")
admin("s:readline(64, sl, 1.0)")
admin("s:error()")
admin("s:readline(64, sl, 1.0)")
admin("s:error()")

# test for case #2-3: timedout, errors
#
print(conn.send("AfricaCubaRomaniaCana"))
admin("s:readline({'Canada'}, 0.01)")
# timedout
admin("s:error()")

print(conn.send("da"))
# should read whole line
admin("s:readline({'Canada'}, 0.01)")
admin("s:error()")

# to ensure readahead pointer correctness
print(conn.send("Canada"))
admin("s:readline({'Canada'}, 0.01)")
admin("s:error()")

# test for case #5: eof testing
#
print(conn.send("msg msg msg msg msg"))
conn.close()

admin("s:readline({'msg'})")
admin("s:error()")

admin("s:readline({'msg'})")
admin("s:error()")

admin("s:readline({'msg'})")
admin("s:error()")

admin("s:readline({'msg'})")
admin("s:error()")

admin("s:readline({'msg'})")
admin("s:error()")

# eof (zero)
admin("s:readline({'msg'})")
admin("s:error()")

# eof (zero)
admin("s:readline({'msg'})")
admin("s:error()")

admin("s:close()")

s.close()

# test for case #5: eof in the middle of a separator
#                   pending
#
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('localhost', 30307))
s.listen(1)
admin("type(s:connect('127.0.0.1', '30307'))")
admin("s:error()")
conn, addr = s.accept()

print(conn.send("SomelongLongStringStrinString"))
conn.close()

# should be returned with eof flag
admin("s:readline({'Z'})")
admin("s:error()")

admin("s:close()")

s.close()

###########################
# simple usage as testing #
###########################

# echo server (test is server) (TCP).
#
# connect from stored procedure to echo server and
# do send/recv.
#
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('localhost', 30303))
s.listen(1)

admin("type(s:connect('localhost', '30303'))")
admin("s:send('ping')")

conn, addr = s.accept()
print('connected')
data = conn.recv(4)
conn.sendall(data)

admin("s:recv(4)")
conn.close()
s.close()

admin("s:send('ping')")
admin("s:error()")

# broken pipe
admin("s:send('ping')")
admin("s:error()")

admin("s:close()")

# echo server (box is server) (TCP).
#
# connect from test to echo server implemented in
# stored procedure and do send/recv.
#
admin("type(s:bind('127.0.0.1', '30303'))")
admin("type(s:listen())")

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.connect(('127.0.0.1', 30303))
s.sendall('ping')

admin("client, status, addr = s:accept()")
admin("addr")
admin("data = client:recv(4)")
admin("data")
admin("client:send(data, 4)")
admin("client:close()")
admin("s:close()")

data = s.recv(4)
s.close()
print(data)

# echo server (test is server) (UDP).
#
# connect from stored procedure to echo server and
# do send/recv.
#
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('localhost', 30302))

# SOCK_DGRAM
admin("s = box.socket.udp()")
admin("type(s:sendto('ping', '127.0.0.1', '30302'))")
admin("s:error()")

data, addr = s.recvfrom(4)
print(data)
s.sendto(data, addr)

admin("s:recv(4)")

s.close()
admin("s:close()")

# echo server (box is server) (UDP).
#
# connect from test to echo server implemented in
# stored procedure and do send/recv.
#
admin("s = box.socket.udp()")
admin("type(s:bind('127.0.0.1', '30301'))")
admin("s:error()")

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.sendto('ping', ('127.0.0.1', 30301))

admin("data, status, client, port = s:recvfrom(4)")
admin("s:error()")
admin("data")
admin("client")

admin("type(s:sendto(data, client, port))")
admin("s:error()")

data = s.recv(4)
print(data)
s.close()

admin("s:close()")

# Bug #1160869: incorrect fiber call order.
# (https://bugs.launchpad.net/tarantool/+bug/1160869)
#
test="""
replies = 0
function bug1160869()
	local s = box.socket.tcp()
	s:connect('127.0.0.1', box.cfg.primary_port)
	box.fiber.resume( box.fiber.create(function()
		box.fiber.detach()
		while true do
			s:recv(12)
			replies = replies + 1
		end
	end) )
	return s:send(box.pack('iii', box.net.PING, 0, 1))
end
"""
admin(test.replace('\n', ' '))
admin("bug1160869()")
admin("bug1160869()")
admin("bug1160869()")
# Due to delays in event loop the value can be not updated
admin("wait_cout = 100 while replies ~= 3 and wait_cout > 0 do box.fiber.sleep(0.001) wait_cout = wait_cout - 1 end")
admin("replies")

test="""
s = nil
syncno = 0
reps = 0
function iostart()
	if s ~= nil then
		return
	end
	s = box.socket.tcp()
	s:connect('127.0.0.1', box.cfg.primary_port)
	box.fiber.resume( box.fiber.create(function()
		box.fiber.detach()
		while true do
			s:recv(12)
			reps = reps + 1
		end
	end))
end

function iotest()
	iostart()
	syncno = syncno + 1
	return s:send(box.pack('iii', box.net.PING, 0, syncno))
end
"""
admin(test.replace('\n', ' '))
admin("iotest()")
admin("iotest()")
admin("iotest()")
# Due to delays in event loop the value can be not updated
admin("wait_cout = 100 while reps ~= 3 and wait_cout > 0 do box.fiber.sleep(0.001) wait_cout = wait_cout - 1 end")
admin("reps")

# Bug #43: incorrect box:shutdown() arg handling
# https://github.com/tarantool/tarantool/issues/43
#
test="""
function server()
	ms = box.socket.tcp()
	ms:bind('127.0.0.1', 8181)
	ms:listen()
	test_listen_done = true

	while true do
		local s = ms:accept( .5 )
		if s ~= 'timeout' then
			print("accepted connection ", s)
			s:send('Hello world')
			s:shutdown(box.socket.SHUT_RDWR)
		end
	end
end

box.fiber.wrap(server)
"""
admin("test_listen_done = false")
admin(test.replace('\n', ' '))
# Due to delays in event loop the value can be not updated
admin("wait_cout = 100 while not test_listen_done and wait_cout > 0 do box.fiber.sleep(0.001) wait_cout = wait_cout - 1 end")


s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(('127.0.0.1', 8181))
data = s.recv(1024)
s.close()
print data

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(('127.0.0.1', 8181))
data = s.recv(1024)
s.close()
print data

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect(('127.0.0.1', 8181))
data = s.recv(1024)
s.close()
print data
admin("ms:close()")
