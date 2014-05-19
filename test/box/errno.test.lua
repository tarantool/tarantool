type(box.errno)
box.errno.EINVAL > 0
box.errno.EBADF > 0
box.errno(box.errno.EINVAL) == box.errno.EINVAL, box.errno() == box.errno.EINVAL
box.errno(box.errno.EBADF) ~= box.errno.EINVAL, box.errno() == box.errno.EBADF
box.errno.strerror(box.errno.EINVAL)
