fio = require 'fio'
errno = require 'errno'

fh1 = fio.open("/tmp/tarantool-test.fio.1", { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, { 'S_IRUSR', 'S_IWUSR' })
fh2 = fio.open("/tmp/tarantool-test.fio.2", { 'O_RDWR', 'O_TRUNC', 'O_CREAT' }, { 'S_IRUSR', 'S_IWUSR' })

type(fh1)
type(fh2)

fh1:seek(123)
fh1:write('Hello, world')

fh1:fdatasync()
fh1:fsync()

fio.stat("/tmp/tarantool-test.fio.1").size

fh1:seek(123)
fh1:read(500)

fh1:truncate(128)
fh1:seek(123)
fh1:read(3)
fh1:read(500)

fh1:seek(123)
fio.truncate("/tmp/tarantool-test.fio.1", 127)
fio.stat("/tmp/tarantool-test.fio.1").size
fh1:stat().size
fh1:read(500)



fh1:close()
fh1:close()
fh2:close()

fio.symlink("/tmp/tarantool-test.fio.1", "/tmp/tarantool-test.fio.3")
fio.readlink("/tmp/tarantool-test.fio.3")
fio.symlink("/tmp/tarantool-test.fio.1", "/tmp/tarantool-test.fio.3")
errno.strerror(errno())

fio.rename("/tmp/tarantool-test.fio.3", "/tmp/tarantool-test.fio.4")
fio.glob("/tmp/tarantool-test.fio.[1-4]")

fio.unlink("/tmp/tarantool-test.fio.1")
fio.unlink("/tmp/tarantool-test.fio.2")
fio.unlink("/tmp/tarantool-test.fio.3")
fio.unlink("/tmp/tarantool-test.fio.4")

fio.stat("/tmp/tarantool-test.fio.1")

fio.glob("/tmp/tarantool-test.fio.[12]")


fio
