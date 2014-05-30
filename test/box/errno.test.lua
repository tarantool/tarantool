errno = require('box.errno')
type(errno)
errno.EINVAL > 0
errno.EBADF > 0
errno(errno.EINVAL) == errno.EINVAL, errno() == errno.EINVAL
errno(errno.EBADF) ~= errno.EINVAL, errno() == errno.EBADF
errno.strerror(errno.EINVAL)
