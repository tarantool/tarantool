fio = require 'fio'
ffi = require 'ffi'
fiber = require 'fiber'
buffer = require 'buffer'
log = require 'log'
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
fh1 = fio.open(file1, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, tonumber('0777', 8))
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

-- gh-3258 rmtree should remove directories with files
fio.mktree('tmp2/tmp3/tmp4')
fh = fio.open('tmp2/tmp3/tmp4/tmp.txt', {'O_RDWR', 'O_CREAT'})
fh:close()
fio.rmtree('tmp2')
fio.stat('tmp2')

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
fio.mktree(tree2, tonumber('0777', 8))

-- check mktree error reporting
--
-- The test is skipped for the super user, because it has ability
-- to perform any file access despite file permissions.
uid = ffi.C.getuid()
tmp3 = fio.pathjoin(tmpdir, '5')
fio.mkdir(tmp3)
fio.chmod(tmp3, tonumber('500', 8))
tree123 = fio.pathjoin(tmp3, '1/2/3')
st, err = fio.mktree(tree123)
uid == 0 or st == false
uid == 0 or err:match('Permission denied') ~= nil
tree4 = fio.pathjoin(tmp3, '4')
st, err = fio.mktree(tree4)
uid == 0 or st == false
uid == 0 or err:match('Permission denied') ~= nil

-- copy and copytree
file1 = fio.pathjoin(tmp1, 'file.1')
file2 = fio.pathjoin(tmp2, 'file.2')
file3 = fio.pathjoin(tree, 'file.3')
file4 = fio.pathjoin(tree, 'file.4')
file5 = fio.pathjoin(tree, 'file.5')
file6 = fio.pathjoin(tree, 'file.6')
file7 = fio.pathjoin(tree, 'file.7')
file8 = fio.pathjoin(tree, 'file.8')

fh1 = fio.open(file1, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, tonumber('0777', 8))
fh1:write("gogo")
fh1:close()
fh1 = fio.open(file2, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, tonumber('0777', 8))
fh1:write("lolo")
fh1:close()
fio.symlink(file1, file3)
fio.copyfile(file1, tmp2)
fio.stat(fio.pathjoin(tmp2, "file.1")) ~= nil

--- test copyfile to operate with one byte transfer
errinj = box.error.injection
errinj.set('ERRINJ_COIO_SENDFILE_CHUNK', 1)
fio.copyfile(file1, file4)
fio.stat(file1, file4) ~= nil
errinj.set('ERRINJ_COIO_SENDFILE_CHUNK', -1)

--- test the destination file is truncated
fh5 = fio.open(file5, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, tonumber('0644', 8))
fh5:write("template data")
fh5:close()
fh6 = fio.open(file6, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, tonumber('0644', 8))
fh6:write("to be truncated")
fio.copyfile(file5, file6)
fh6:seek(0)
fh6:read()
fh6:close()

--
-- gh-4651: Test partial write/pwrite via one byte transfer.
--
fh7 = fio.open(file7, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, tonumber('0644', 8))
errinj.set('ERRINJ_COIO_WRITE_CHUNK', true)
fh7:write("one byte transfer, write")
errinj.set('ERRINJ_COIO_WRITE_CHUNK', false)
fh7:seek(0)
fh7:read()
fh7:close()
fh8 = fio.open(file8, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, tonumber('0644', 8))
errinj.set('ERRINJ_COIO_WRITE_CHUNK', true)
fh8:pwrite("one byte transfer, ", 0)
fh8:seek(0)
fh8:read()
fh8:pwrite("pwrite", 19)
errinj.set('ERRINJ_COIO_WRITE_CHUNK', false)
fh8:seek(0)
fh8:read()
fh8:close()

res, err = fio.copyfile(fio.pathjoin(tmp1, 'not_exists.txt'), tmp1)
res
err:match("failed to copy") ~= nil

newdir = fio.pathjoin(tmpdir, "newdir")
fio.copytree(fio.pathjoin(tmpdir, "1"), newdir)
fio.stat(fio.pathjoin(newdir, "file.1")) ~= nil
fio.stat(fio.pathjoin(newdir, "2", "file.2")) ~= nil
fio.stat(fio.pathjoin(newdir, "2", "3", "file.3")) ~= nil
fio.readlink(fio.pathjoin(newdir, "2", "3", "file.3")) == file1
fio.rmtree(tmpdir)
fio.copytree("/no/such/dir", "/some/where")

-- ibuf read/write
buf = buffer.ibuf()

tmpdir = fio.tempdir()
tmpfile = fio.pathjoin(tmpdir, "test1")
fh = fio.open(tmpfile, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, tonumber('0777', 8))
fh:write('helloworld!')
fh:seek(0)
fh:read()
fh:close()
fh:read()
fio.unlink(tmpfile)

tmpfile = fio.pathjoin(tmpdir, "test")
fh = fio.open(tmpfile, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, tonumber('0777', 8))
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

-- test utime
test_run:cmd("push filter '(.builtin/.*.lua):[0-9]+' to '\\1'")
fh = fio.open('newfile', {'O_RDWR','O_CREAT'})
current_time = math.floor(fiber.time())
fiber_time = fiber.time
fiber.time = function() return current_time end
fio.utime('newfile', 0, 0)
fh:stat().atime == 0
fh:stat().mtime == 0
fio.utime('newfile', 1, 2)
fh:stat().atime == 1
fh:stat().mtime == 2
fio.utime('newfile', 3)
fh:stat().atime == 3
fh:stat().mtime == 3
fio.utime('newfile')
fh:stat().atime == current_time
fh:stat().mtime == current_time
fio.utime(nil)
fio.utime('newfile', 'string')
fio.utime('newfile', 1, 'string')
fh:close()
fio.unlink('newfile')
fh = nil
current_time = nil
fiber.time = fiber_time
fiber_time = nil

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
fio.rmdir(tmpdir)

--
-- gh-3580: Check that error messages are descriptive enough.
--
fh1:seek(nil, 'a')
fio.open(nil)
fio.open(tmp1, {'A'}, tonumber('0777', 8))
fio.open(tmp1, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, {'A'})
fio.pathjoin(nil)
fio.pathjoin('abc', nil)
fio.pathjoin('abc', 'cde', nil)
fio.basename(nil)
fio.abspath(nil)
fio.chdir(1)
fio.listdir(1)
fio.mktree(1)
fio.rmtree(1)
fio.copytree(nil, nil)
fio.copytree(nil, nil)
test_run:cmd("clear filter")

--
-- gh-4439: mktree error handling fix - creation of existing file/directory
--
fio.mktree('/dev/null')
fio.mktree('/dev/null/dir')

--
-- gh-4794: fio.tempdir() should use $TMPDIR.
--
cwd = fio.cwd()
old_tmpdir = os.getenv('TMPDIR')

tmpdir = cwd..'/tmp-.dot.-'
fio.mkdir(tmpdir)
os.setenv('TMPDIR', tmpdir)
dir = fio.tempdir()
dir:startswith(tmpdir) or log.error({dir, tmpdir})
fio.stat(dir) ~= nil or log.error(fio.stat(dir))

tmpdir = cwd..'/tmp2'
os.setenv('TMPDIR', tmpdir)
fio.tempdir()

os.setenv('TMPDIR', nil)
dir = fio.tempdir()
dir:startswith('/tmp') or dir

tmpdir = cwd..'/'..string.rep('t', 5000)
os.setenv('TMPDIR', tmpdir)
fio.tempdir()
tmpdir = nil

os.setenv('TMPDIR', old_tmpdir)

--
-- read() and pread() should not use a shared buffer so as not to clash with
-- other fibers during yield.
--
rights = tonumber('0777', 8)
flags = {'O_RDWR', 'O_TRUNC', 'O_CREAT'}
tmpdir = fio.tempdir()
file1 = fio.pathjoin(tmpdir, 'file1')
file2 = fio.pathjoin(tmpdir, 'file2')
fd1 = fio.open(file1, flags, rights)
fd2 = fio.open(file2, flags, rights)
fd1:write('1'), fd1:seek(0)
fd2:write('2'), fd2:seek(0)

res1, res2 = nil
do                                                                              \
    fiber.new(function() res1 = fd1:read() end)                                 \
    fiber.new(function() res2 = fd2:read() end)                                 \
end
_ = test_run:wait_cond(function() return res1 and res2 end)
assert(res1 == '1')
assert(res2 == '2')
--
-- The same with pread().
--
res1, res2 = nil
do                                                                              \
    fiber.new(function() res1 = fd1:pread(1, 0) end)                            \
    fiber.new(function() res2 = fd2:pread(1, 0) end)                            \
end
_ = test_run:wait_cond(function() return res1 and res2 end)
assert(res1 == '1')
assert(res2 == '2')

fd1:close()
fd2:close()
fio.rmtree(tmpdir)
