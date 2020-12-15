#!/usr/bin/env tarantool

local function table2str(t)
    local res = ""
    for _, line in pairs(t) do
        local s = ""
        for _, field in pairs(line) do
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
local test1_ans = '|a|\t|b|\t\n|1|\t|ha\n"ha"\nha|\t\n|3|\t|4|\t\n'
local test2_ans = '||\t||\t||\t\n||\t||\t\n||\t\n'
local test3_ans = '||\t||\t\n|kp"v|\t\n'
local test4_ans = '|123|\t|5|\t|92|\t|0|\t|0|\t\n|1|\t|12  34|\t|56|\t' ..
                  '|quote , |\t|66|\t\n|ok|\t\n'
local test5_ans = "|1|\t\n|23|\t|456|\t|abcac|\t|'multiword field 4'|\t\n" ..
                  "|none|\t|none|\t|0|\t\n||\t||\t||\t\n|aba|\t|adda|\t|f" ..
                  "3|\t|0|\t\n|local res = internal.pwrite(self.fh|\t|dat" ..
                  "a|\t|len|\t|offset)|\t\n|iflag = bit.bor(iflag|\t|fio." ..
                  "c.flag[ flag ])|\t\n||\t||\t||\t\n"
local test6_ans = "|23|\t|456|\t|abcac|\t|'multiword field 4'|\t\n|none|" ..
                  "\t|none|\t|0|\t\n||\t||\t||\t\n|aba|\t|adda|\t|f3|\t|" ..
                  "0|\t\n|local res = internal.pwrite(self.fh|\t|data|\t" ..
                  "|len|\t|offset)|\t\n|iflag = bit.bor(iflag|\t|fio.c.f" ..
                  "lag[ flag ])|\t\n||\t||\t||\t\n"

local test = tap.test("csv")
test:plan(12)

local readable = {}
readable.read = myread
readable.v = "a,b\n1,\"ha\n\"\"ha\"\"\nha\"\n3,4\n"
readable.i = 0
test:is(table2str(csv.load(readable)), test1_ans, "obj test1")

readable.v = ", ,\n , \n\n"
readable.i = 0
test:is(table2str(csv.load(readable, {chunk_size = 1} )), test2_ans, "obj test2")

readable.v = ", \r\nkp\"\"v"
readable.i = 0
test:is(table2str(csv.load(readable, {chunk_size = 3})), test3_ans, "obj test3")

local tmpdir = fio.tempdir()
local file1 = fio.pathjoin(tmpdir, 'file.1')
local file2 = fio.pathjoin(tmpdir, 'file.2')
local file3 = fio.pathjoin(tmpdir, 'file.3')
local file4 = fio.pathjoin(tmpdir, 'file.4')

local f = fio.open(file1, { 'O_WRONLY', 'O_TRUNC', 'O_CREAT' }, tonumber('0777', 8))
f:write("123 , 5  ,       92    , 0, 0\n" ..
        "1, 12  34, 56, \"quote , \", 66\nok")
f:close()
f = fio.open(file1, {'O_RDONLY'})
test:is(table2str(csv.load(f, {chunk_size = 10})), test4_ans, "fio test1")
f:close()


f = fio.open(file2, { 'O_WRONLY', 'O_TRUNC', 'O_CREAT' }, tonumber('0777', 8))
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
--symbol by symbol reading
test:is(table2str(csv.load(f, {chunk_size = 1})), test5_ans, "fio test2")
f:close()

f = fio.open(file2, {'O_RDONLY'})
local opts = {chunk_size = 7, skip_head_lines = 1}
--7 symbols per chunk
test:is(table2str(csv.load(f, opts)), test6_ans, "fio test3")
f:close()

local t = {
          {'quote" d', ',and, comma', 'both " of " t,h,e,m'},
          {'"""', ',","'},
          {'mul\nti\nli\r\nne\n\n', 'field'},
          {""},
          {'"'},
          {"\n"}
}

f = require("fio").open(file3, { "O_WRONLY", "O_TRUNC" , "O_CREAT"}, 0x1FF)
csv.dump(t, {}, f)
f:close()
f = fio.open(file3, {'O_RDONLY'})
local t2 = csv.load(f, {chunk_size = 5})
f:close()

test:is(table2str(t), table2str(t2), "test roundtrip")

test:is(table2str(t), table2str(csv.load(csv.dump(t))), "test load(dump(t))")

test:is(table2str(csv.load('a,b,c,')), '|a|\t|b|\t|c|\t||\t\n', "final comma")

local str = "ячсмитьб-Pincall;79031111111\r\n"
str = str .. str .. str .. str .. str .. str
str = "Vendor;Prefix\r\n" .. str
f = fio.open(file4, { "O_WRONLY", "O_TRUNC" , "O_CREAT"}, 0x1FF)
f:write(str)
f:close()

test:is(#csv.load(fio.open(file4, {'O_RDONLY'}),
                  {separator = ';', chunk_size = 3}), 7, "gh-1210 (1)")
test:is(#csv.load(fio.open(file4, {'O_RDONLY'}),
                  {separator = ';', chunk_size = 4}), 7, "gh-1210 (2)")

fio.unlink(file1)
fio.unlink(file2)
fio.unlink(file3)
fio.unlink(file4)
fio.rmdir(tmpdir)

-- gh-3489: crash with ending space and empty field
local res = csv.load('929,N1XDN ,,"Enfield, CT",')
local exp = {{'929', 'N1XDN', '', 'Enfield, CT', ''}}
test:is_deeply(res, exp, 'gh-3489')

test:check()
