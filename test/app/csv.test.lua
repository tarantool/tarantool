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

print("obj test1:")
readable = {}
readable.read = myread
readable.v = "a,b\n1,\"ha\n\"\"ha\"\"\nha\"\n3,4\n"
readable.i = 0
print(table2str(csv.load(readable)))

print("obj test2:")
readable.v = ", ,\n , \n\n"
readable.i = 0
print(table2str(csv.load(readable, 1)))

print("obj test3:")
readable.v = ", \r\nkp\"\"v"
readable.i = 0
print(table2str(csv.load(readable, 3)))

tmpdir = fio.tempdir()
file1 = fio.pathjoin(tmpdir, 'file.1')
file2 = fio.pathjoin(tmpdir, 'file.2')
file3 = fio.pathjoin(tmpdir, 'file.3')

print("fio test1:")
local f = fio.open(file1, { 'O_WRONLY', 'O_TRUNC', 'O_CREAT' }, 0777)
f:write("123 , 5  ,       92    , 0, 0\n" ..
        "1, 12  34, 56, \"quote , \", 66\nok")
f:close()
f = fio.open(file1, {'O_RDONLY'}) 
print(table2str(csv.load(f,10)))
f:close()


print("fio test2:")
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

print("fio test3:")
f = fio.open(file2, {'O_RDONLY'}) 
print(table2str(csv.load(f, 1))) --symbol by symbol reading
f:close()

print("fio test4:")
f = fio.open(file2, {'O_RDONLY'}) 
print(table2str(csv.load(f, 7))) --7 symbols per chunk
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
csv.dump(f, t)
f:close()
f = fio.open(file3, {'O_RDONLY'}) 
t2 = csv.load(f, 5)
f:close()

print("test roundtrip: ")
print(table2str(t) == table2str(t2))

fio.unlink(file1)
fio.unlink(file2)
fio.unlink(file3)
fio.rmdir(tmpdir)
