# -*- coding: utf-8 -*-
# pylint: disable=C0301,W0105,W0401,W0614
'''
Python DB API compatible exceptions
http://www.python.org/dev/peps/pep-0249/

The PEP-249 says that database related exceptions must be inherited as follows:

    StandardError
    |__Warning
    |__Error
       |__InterfaceError
       |__DatabaseError
          |__DataError
          |__OperationalError
          |__IntegrityError
          |__InternalError
          |__ProgrammingError
          |__NotSupportedError
'''

import os
import socket
import sys
import warnings


class Error(StandardError):
    '''Base class for error exceptions'''


class DatabaseError(Error):
    '''Error related to the database engine'''


class InterfaceError(Error):
    '''Error related to the database interface rather than the database itself'''



# Monkey patch os.strerror for win32
if sys.platform == "win32":
    # Windows Sockets Error Codes (not all, but related on network errors)
    # http://msdn.microsoft.com/en-us/library/windows/desktop/ms740668(v=vs.85).aspx
    _code2str = {
        10004: "Interrupted system call",
        10009: "Bad file descriptor",
        10013: "Permission denied",
        10014: "Bad address",
        10022: "Invalid argument",
        10024: "Too many open files",
        10035: "Resource temporarily unavailable",
        10036: "Operation now in progress",
        10037: "Operation already in progress",
        10038: "Socket operation on nonsocket",
        10039: "Destination address required",
        10040: "Message too long",
        10041: "Protocol wrong type for socket",
        10042: "Bad protocol option",
        10043: "Protocol not supported",
        10044: "Socket type not supported",
        10045: "Operation not supported",
        10046: "Protocol family not supported",
        10047: "Address family not supported by protocol family",
        10048: "Address already in use",
        10049: "Cannot assign requested address",
        10050: "Network is down",
        10051: "Network is unreachable",
        10052: "Network dropped connection on reset",
        10053: "Software caused connection abort",
        10054: "Connection reset by peer",
        10055: "No buffer space available",
        10056: "Socket is already connected",
        10057: "Socket is not connected",
        10058: "Cannot send after transport endpoint shutdown",
        10060: "Connection timed out",
        10061: "Connection refused",
        10062: "Cannot translate name",
        10063: "File name too long",
        10064: "Host is down",
        10065: "No route to host",
        11001: "Host not found",
        11004: "Name or service not known"
    }


    os_strerror_orig = os.strerror

    def os_strerror_patched(code):
        '''\
        Return cross-platform message about socket-related errors

        This function exists because under Windows os.strerror returns 'Unknown error' on all socket-related errors.
        And socket-related exception contain broken non-ascii encoded messages.
        '''
        message = os_strerror_orig(code)
        if not message.startswith("Unknown"):
            return message
        else:
            return _code2str.get(code, "Unknown error %s"%code)

    os.strerror = os_strerror_patched


class NetworkError(DatabaseError):
    '''Error related to network'''
    def __init__(self, orig_exception=None, *args):
        if orig_exception:
            if isinstance(orig_exception, socket.timeout):
                self.message = "Socket timeout"
                super(NetworkError, self).__init__(0, self.message)
            elif isinstance(orig_exception, socket.error):
                self.message = os.strerror(orig_exception.errno)
                super(NetworkError, self).__init__(orig_exception.errno, self.message)
            else:
                super(NetworkError, self).__init__(orig_exception, *args)


class NetworkWarning(UserWarning):
    '''Warning related to network'''
    pass


class RetryWarning(UserWarning):
    '''Warning is emited in case of server return completion_status == 1 (try again)'''
    pass


# always print this warnings
warnings.filterwarnings("always", category=NetworkWarning)
warnings.filterwarnings("always", category=RetryWarning)


def warn(message, warning_class):
    '''\
    Emit warinig message.
    Just like standard warnings.warn() but don't output full filename.
    '''
    frame = sys._getframe(2) # pylint: disable=W0212
    module_name = frame.f_globals.get("__name__")
    line_no = frame.f_lineno
    warnings.warn_explicit(message, warning_class, module_name, line_no)

