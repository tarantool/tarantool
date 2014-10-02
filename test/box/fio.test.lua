fio = require 'fio'
errno = require 'errno'

-- umask

type(fio.umask(0))
fio.umask()

-- pathjoin
fio.pathjoin('abc', 'cde')
fio.pathjoin('/', 'abc')
fio.pathjoin('abc/', '/cde')
fio.pathjoin('/', '/cde')

-- basename
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

fio.link(file1, file2)

glob = fio.glob(fio.pathjoin(tmpdir, '*'))
#glob
{ string.match(glob[1], '^.*/(.*)'), string.match(glob[2], '^.*/(.*)') }
fio.stat(file1).inode == fio.stat(file2).inode

fh3 = fio.open(file3, { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, 0x1FD)
fh1:stat().inode ~= fh3:stat().inode
0775
bit.band(fh3:stat().mode, 0x1FF) == 0x1FD
fh3:write("abc")


fio.rename(file3, file4)
fio.symlink(file4, file3)
fio.stat(file3).size
fio.lstat(file3).size ~= fio.stat(file3).size
fio.lstat(file3).mode ~= fio.stat(file3).mode
fio.basename(fio.readlink(file3))

bit.band(fio.stat(file4).mode, 0x1FF) == 0x1FD
fio.chmod(file4, 0x1F8) -- 0x770
bit.band(fh3:stat().mode, 0x1FF) == 0x1F8
bit.band(fio.stat(file4).mode, 0x1FF) == 0x1F8


fio.mkdir(fio.pathjoin(tmpdir, "dir"))

-- cleanup directories
{ fh1:close(), fh3:close() }
{ fh1:close(), errno.strerror(), fh3:close(), errno.strerror() }

fio.rmdir(fio.pathjoin(tmpdir, "dir"))

{ fio.unlink(file1), fio.unlink(file2), fio.unlink(file3), fio.unlink(file4) }
{ fio.unlink(file1), fio.unlink(file2), fio.unlink(file3), fio.unlink(file4) }
fio.rmdir(tmpdir)
{ fio.rmdir(tmpdir), errno.strerror() }

fio.unlink()
fio.unlink(nil)

-- dirname

fio.dirname('abc')
fio.dirname('/abc')
fio.dirname('/abc/cde')
fio.dirname('/abc/cde/')
fio.dirname('/')
