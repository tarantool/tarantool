#!/usr/bin/env tarantool

local function table2str(t)
    res = ""
    for k, line in pairs(t) do
        s = ""
        for k2, field in pairs(line) do
            s = s .. '|' .. field .. '|\t'
        end
        res = res .. s .. '\n'
    end
    return res
end

local function myread(self, bytes) 
    self.i = self.i + bytes
    return self.v:sub(self.i - bytes + 1, self.i) 
end
local csv = require('csv')
local fio = require('fio')
local tap = require('tap')
local test1 = '|a|\t|b|\t\n|1|\t|ha\n"ha"\nha|\t\n|3|\t|4|\t\n'
local test2 = '||\t||\t||\t\n||\t||\t\n||\t\n'
local test3 = '||\t||\t\n|kp"v|\t\n'
local test4 = '|123|\t|5|\t|92|\t|0|\t|0|\t\n|1|\t|12  34|\t|56|\t|quote , |\t|66|\t\n|ok|\t\n'
local test5 = "|1|\t\n|23|\t|456|\t|abcac|\t|'multiword field 4'|\t\n|none|\t|none|\t|0|\t\n" .. 
        "||\t||\t||\t\n|aba|\t|adda|\t|f3|\t|0|\t\n|local res = internal.pwrite(self.fh|\t|d" ..
        "ata|\t|len|\t|offset)|\t\n|iflag = bit.bor(iflag|\t|fio.c.flag[ flag ])|\t\n||\t||\t||\t\n"
local test6 = "|23|\t|456|\t|abcac|\t|'multiword field 4'|\t\n|none|\t|none|\t|0|\t\n||\t||\t||\t\n" .. 
        "|aba|\t|adda|\t|f3|\t|0|\t\n|local res = internal.pwrite(self.fh|\t|data|\t|len|\t|offset)" ..
        "|\t\n|iflag = bit.bor(iflag|\t|fio.c.flag[ flag ])|\t\n||\t||\t||\t\n"

test = tap.test("csv")
test:plan(8)

readable = {}
readable.read = myread
readable.v = "a,b\n1,\"ha\n\"\"ha\"\"\nha\"\n3,4\n"
readable.i = 0
test:is(table2str(csv.load(readable)), test1, "obj test1")

readable.v = ", ,\n , \n\n"
readable.i = 0
test:is(table2str(csv.load(readable, 0, 1)), test2, "obj test2")

readable.v = ", \r\nkp\"\"v"
readable.i = 0
test:is(table2str(csv.load(readable, 0, 3)), test3, "obj test3")

tmpdir = fio.tempdir()
file1 = fio.pathjoin(tmpdir, 'file.1')
file2 = fio.pathjoin(tmpdir, 'file.2')
file3 = fio.pathjoin(tmpdir, 'file.3')

local f = fio.open(file1, { 'O_WRONLY', 'O_TRUNC', 'O_CREAT' }, 0777)
f:write("123 , 5  ,       92    , 0, 0\n" ..
        "1, 12  34, 56, \"quote , \", 66\nok")
f:close()
f = fio.open(file1, {'O_RDONLY'}) 
test:is(table2str(csv.load(f,0,10)), test4, "fio test1")
f:close()


f = fio.open(file2, { 'O_WRONLY', 'O_TRUNC', 'O_CREAT' }, 0777)
f:write("1\n23,456,abcac,\'multiword field 4\'\n" ..
        "none,none,0\n" ..
        ",,\n" ..
        "aba,adda,f3,0\n" ..
        "local res = internal.pwrite(self.fh, data, len, offset)\n" ..
        "iflag = bit.bor(iflag, fio.c.flag[ flag ])\n" ..
        ",,"
)
f:close()
f = fio.open(file2, {'O_RDONLY'}) 
test:is(table2str(csv.load(f, 0, 1)), test5, "fio test2") --symbol by symbol reading
f:close()

f = fio.open(file2, {'O_RDONLY'}) 
test:is(table2str(csv.load(f, 1, 7)), test6, "fio test3") --7 symbols per chunk
f:close()


t = {
    {'quote" d', ',and, comma', 'both " of " t,h,e,m'}, 
    {'"""', ',","'}, 
    {'mul\nti\nli\r\nne\n\n', 'field'},
    {""},
    {'"'},
    {"\n"}
}

f = require("fio").open(file3, { "O_WRONLY", "O_TRUNC" , "O_CREAT"}, 0x1FF)
csv.dump(t, f)
f:close()
f = fio.open(file3, {'O_RDONLY'}) 
t2 = csv.load(f, 0, 5)
f:close()

test:is(table2str(t), table2str(t2), "test roundtrip")

test:is(table2str(t), table2str(csv.load(csv.dump(t))), "test load(dump(t))")

fio.unlink(file1)
fio.unlink(file2)
fio.unlink(file3)
fio.rmdir(tmpdir)
