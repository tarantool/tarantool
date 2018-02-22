fio = require 'fio'
ffi = require 'ffi'
buffer = require 'buffer'
test_run = require('test_run').new()
-- umask

type(fio.umask(0))
fio.umask()

-- pathjoin
st, err = pcall(fio.basename, nil, nil)
st
err:match("basename") ~= nil
fio.pathjoin('abc', 'cde')
fio.pathjoin('/', 'abc')
fio.pathjoin('abc/', '/cde')
fio.pathjoin('/', '/cde')
fio.pathjoin('/a', '/')
fio.pathjoin('abc', 'awdeq///qweqwqwe///', "//asda//")

-- basename
st, err = pcall(fio.basename, nil)
st
err:match("basename") ~= nil
fio.basename('/')
fio.basename('abc')
fio.basename('abc.cde', '.cde')
fio.basename('abc^cde', '.cde')
fio.basename('/path/to/file.cde', '.cde')



-- other tests
tmpdir = fio.tempdir()

file1 = fio.pathjoin(tmpdir, 'file.1')
file2 = fio.pathjoin(tmpdir, 'file.2')
file3 = fio.pathjoin(tmpdir, 'file.3')
file4 = fio.pathjoin(tmpdir, 'file.4')


st, err = pcall(fio.open, nil)
st
err:match("open") ~= nil
fh1 = fio.open(file1, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, 0777)
fh1 ~= nil
f1s = fh1:stat()
f1s.size

f1s.is_reg()
f1s:is_reg()
f1s:is_dir()
f1s:is_link()
f1s:is_sock()
f1s:is_fifo()
f1s:is_chr()
f1s:is_blk()

fh1:seek(121)
fh1:stat().size
fh1:write(nil)
fh1:write("Hello, world")
fh1:stat().size
fh1:fsync()
fh1:fdatasync()
fio.sync()
fh1:pread(512, 121)
fh1:pread(5, 121)

fh1:write("; Ehllo, again")
fh1:seek(121)
fh1:read(13)
fh1:read(512)
fh1:pread(512, 14 + 121)
fh1:pwrite("He", 14 + 121)
fh1:pread(512, 14 + 121)

{ fh1:stat().size, fio.stat(file1).size }
fh1:seek(121)
fh1:read(512)

fio.link(nil, nil)
fio.link(file1, file2)

fio.glob(nil)
glob = fio.glob(fio.pathjoin(tmpdir, '*'))
#glob
{ string.match(glob[1], '^.*/(.*)'), string.match(glob[2], '^.*/(.*)') }
fio.stat(file1).inode == fio.stat(file2).inode

fh3 = fio.open(file3, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, 0x1FD)
fh1:stat().inode ~= fh3:stat().inode
0775
bit.band(fh3:stat().mode, 0x1FF) == 0x1FD
fh3:write("abc")


fio.rename(nil, nil)
fio.rename(file3, file4)
fio.symlink(nil, nil)
fio.symlink(file4, file3)
fio.stat(nil)
fio.stat(file3).size
fio.lstat(file3).size ~= fio.stat(file3).size
fio.lstat(file3).mode ~= fio.stat(file3).mode
fio.basename(fio.readlink(file3))

bit.band(fio.stat(file4).mode, 0x1FF) == 0x1FD
fio.chmod(nil, 0x1F8)
fio.chmod(file4, 0x1F8) -- 0x770
bit.band(fh3:stat().mode, 0x1FF) == 0x1F8
bit.band(fio.stat(file4).mode, 0x1FF) == 0x1F8


dir1 = fio.pathjoin(tmpdir, 'dir1')
dir2 = fio.pathjoin(tmpdir, 'dir2')
fio.mkdir(nil)
fio.mkdir(dir1) -- standard mode
fio.mkdir(dir2, 1) -- custom mode
string.format('%04o', bit.band(fio.stat(dir1).mode, 0x1FF))
string.format('%04o', bit.band(fio.stat(dir2).mode, 0x1FF))

-- cleanup directories
{ fh1:close(), fh3:close() }
fh1:close()
fh3:close()

fio.rmdir(nil)
fio.rmdir(dir1)
fio.rmdir(dir2)

{ fio.unlink(file1), fio.unlink(file2), fio.unlink(file3), fio.unlink(file4) }
{ fio.unlink(file1), fio.unlink(file2), fio.unlink(file3), fio.unlink(file4) }
fio.rmdir(tmpdir)
fio.rmdir(tmpdir)

fio.unlink()
fio.unlink(nil)

-- gh-1211 use 0777 if mode omitted in open
fh4 = fio.open('newfile', {'O_RDWR','O_CREAT','O_EXCL'})
bit.band(fh4:stat().mode, 0x1FF) == bit.band(fio.umask(), 0x1ff)
fh4:close()
fio.unlink('newfile')

-- dirname

st, err = pcall(fio.dirname, nil)
st
err:match("dirname") ~= nil
fio.dirname('abc')
fio.dirname('/abc')
fio.dirname('/abc/cde')
fio.dirname('/abc/cde/')
fio.dirname('/')

-- abspath
st, err = pcall(fio.abspath, nil)
st
err:match("abspath") ~= nil
fio.abspath("/")
fio.abspath("/tmp")
fio.abspath("/tmp/test/../")
fio.abspath("/tmp/test/../abc")
fio.abspath("/tmp/./test")
fio.abspath("/tmp///test//abc")
fio.abspath("/../")
fio.abspath("/../tmp")
type(string.find(fio.abspath("tmp"), "tmp"))

-- chdir
old_cwd = fio.cwd()
st, err = pcall(fio.chdir, nil)
st
err:match("chdir") ~= nil
st, err = pcall(fio.chdir, 42)
st
err:match("chdir") ~= nil
fio.chdir('/no/such/file/or/directory')
fio.chdir('/')
fio.cwd()
fio.chdir(old_cwd)
fio.cwd() == old_cwd

-- listdir
tmpdir = fio.tempdir()
dir3 = fio.pathjoin(tmpdir, "dir3")
st, err = pcall(fio.mkdir, nil)
st
err:match("mkdir") ~= nil
fio.mkdir(dir3)
fio.mkdir(fio.pathjoin(dir3, "1"))
fio.mkdir(fio.pathjoin(dir3, "2"))
fio.mkdir(fio.pathjoin(dir3, "3"))
fio.listdir("/no/such/directory/")
ls = fio.listdir(dir3)
table.sort(ls, function(a, b) return tonumber(a) < tonumber(b) end)
ls

-- rmtree
fio.stat(dir3) ~= nil
fio.rmtree(dir3)
fio.stat(dir3) == nil
st, err = fio.rmtree(dir3)
st
err:match("No such") ~= nil

-- mktree
tmp1 = fio.pathjoin(tmpdir, "1")
tmp2 = fio.pathjoin(tmp1, "2")
tree = fio.pathjoin(tmp2, "3")
tree2 = fio.pathjoin(tmpdir, "4")
st, err = pcall(fio.mktree, nil)
st
err:match("mktree") ~= nil
fio.mktree(tree)
fio.stat(tree) ~= nil
fio.stat(tmp2) ~= nil
fio.mktree(tree2, 1)

-- copy and copytree
file1 = fio.pathjoin(tmp1, 'file.1')
file2 = fio.pathjoin(tmp2, 'file.2')
file3 = fio.pathjoin(tree, 'file.3')

fh1 = fio.open(file1, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, 0777)
fh1:write("gogo")
fh1:close()
fh1 = fio.open(file2, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, 0777)
fh1:write("lolo")
fh1:close()
fio.symlink(file1, file3)
fio.copyfile(file1, tmp2)
fio.stat(fio.pathjoin(tmp2, "file.1")) ~= nil

res, err = fio.copyfile(fio.pathjoin(tmp1, 'not_exists.txt'), tmp1)
res
err:match("failed to copy") ~= nil

newdir = fio.pathjoin(tmpdir, "newdir")
fio.copytree(fio.pathjoin(tmpdir, "1"), newdir)
fio.stat(fio.pathjoin(newdir, "file.1")) ~= nil
fio.stat(fio.pathjoin(newdir, "2", "file.2")) ~= nil
fio.stat(fio.pathjoin(newdir, "2", "3", "file.3")) ~= nil
fio.readlink(fio.pathjoin(newdir, "2", "3", "file.3")) == file1
fio.copytree("/no/such/dir", "/some/where")

-- ibuf read/write
buf = buffer.ibuf()

tmpdir = fio.tempdir()
tmpfile = fio.pathjoin(tmpdir, "test1")
fh = fio.open(tmpfile, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, 0777)
fh:write('helloworld!')
fh:seek(0)
fh:read()
fh:close()
fh:read()
fio.unlink(tmpfile)

tmpfile = fio.pathjoin(tmpdir, "test")
fh = fio.open(tmpfile, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, 0777)
fh:write('helloworld!')
fh:seek(0)
len = fh:read(buf:reserve(12))
ffi.string(buf:alloc(len), len)
fh:seek(0)

len = fh:read(buf:reserve(5), 5)
ffi.string(buf:alloc(len), len)
len = fh:read(buf:reserve(5), 5)
ffi.string(buf:alloc(len), len)
len = fh:read(buf:reserve(5), 5)
ffi.string(buf:alloc(len), len)

buf:reset()
len = fh:pread(buf:reserve(5), 5, 5)
ffi.string(buf:alloc(len), len)
len = fh:pread(buf:reserve(5), 5)
ffi.string(buf:alloc(len), len)

fh:seek(0)
fh:write(buf.rpos, buf:size())

fh:seek(0)
fh:read(64)

fh:pwrite(buf:read(5), 5, 5)
fh:pwrite(buf:read(5), 5)

fh:seek(0)
fh:read(64)

buf:recycle()
fh:close()

-- gh-2924
-- fio.path.exists lexists is_file, etc
--
fio.path.is_file(tmpfile)
fio.path.is_dir(tmpfile)
fio.path.is_link(tmpfile)
fio.path.exists(tmpfile)
fio.path.lexists(tmpfile)

non_existing_file = "/no/such/file"
fio.path.is_file(non_existing_file)
fio.path.is_dir(non_existing_file)
fio.path.is_link(non_existing_file)
fio.path.exists(non_existing_file)
fio.path.lexists(non_existing_file)

fio.path.is_file(tmpdir)
fio.path.is_dir(tmpdir)
fio.path.is_link(tmpdir)
fio.path.exists(tmpdir)
fio.path.lexists(tmpdir)

link = fio.pathjoin(tmpdir, "link")
fio.symlink(tmpfile, link)
fio.path.is_file(link)
fio.path.is_dir(link)
fio.path.is_link(link)
fio.path.exists(link)
fio.path.lexists(link)
fio.unlink(link)

fio.symlink(non_existing_file, link)
fio.path.is_file(link)
fio.path.is_dir(link)
fio.path.is_link(link)
fio.path.exists(link)
fio.path.lexists(link)
fio.unlink(link)

fio.symlink(tmpdir, link)
fio.path.is_file(link)
fio.path.is_dir(link)
fio.path.is_link(link)
fio.path.exists(link)
fio.path.lexists(link)

fio.unlink(link)
fio.unlink(tmpfile)
tmp1 = fio.pathjoin(tmpdir, "tmp1")
tmp2= fio.pathjoin(tmpdir, "tmp2")
test_run:cmd("setopt delimiter ';'")
function write_file(name, odd)
    local fh = fio.open(name, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, 0777)
    if odd then
        fh:write(string.rep('1', 100))
    else
        fh:write(string.rep('2', 100))
    end
    fh:write(name)
    fh:seek(0)
    return fh
end;
test_run:cmd("setopt delimiter ''");
fh1 = write_file(tmp1)
fh2 = write_file(tmp2)
fiber = require('fiber')
digest = require('digest')
str = fh1:read()
fh1:seek(0)
hash = digest.crc32(str)
ch = fiber.channel(1)
f1 = fiber.create(function() str = fh1:read() ch:put(digest.crc32(str)) end)
f2 = fiber.create(function() str = fh2:read() end)
ch:get() == hash

fio.unlink(tmp1)
fio.unlink(tmp2)
fio.rmdir(tmpdir)
