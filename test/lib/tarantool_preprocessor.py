import codecs
import tokenize
import cStringIO
import encodings
import pdb
import re
from encodings import utf_8
import sys


def tarantool_translate(readline):
    token_stream = tokenize.generate_tokens(readline)
    for token in token_stream:
        type, name = token[:2]
        if type == tokenize.NAME and name == "exec":
            next_token = next(token_stream)
            type, name = next_token[:2]
            if type == tokenize.NAME and name in [ "sql", "admin", "memcached" ]:
                yield (tokenize.NAME, 'print') + token[2:]
                yield (tokenize.OP, '>>') + token[2:]
                yield next_token
                yield (tokenize.OP, ',') + next_token[2:]
            else:
                yield token
                yield next_token
        else:
            yield token


class TarantoolStreamReader(utf_8.StreamReader):
    def __init__(self, *args, **kwargs):
        utf_8.StreamReader.__init__(self, *args, **kwargs)
        try:
            data = tokenize.untokenize(tarantool_translate(self.stream.readline))
            self.stream = cStringIO.StringIO(data)
        except Exception:
            self.stream.seek(0)


def tarantool_encoding_builder(encoding_name):
    """Return an encoding that pre-processes the input and
    rewrites it to be pure python"""
    if encoding_name == "tarantool":
        utf8 = encodings.search_function("utf8")
        return codecs.CodecInfo(name = "tarantool",
                                encode = utf8.encode,
                                decode = utf8.decode,
                                incrementalencoder = utf8.incrementalencoder,
                                incrementaldecoder = utf8.incrementaldecoder,
                                streamreader = TarantoolStreamReader,
                                streamwriter = utf8.streamwriter)
    return None

codecs.register(tarantool_encoding_builder)


def main():
    py_input = """exec admin 'show info'
print 'hello'
exec sql 'select * from namespace1'\n"""
    print py_input
    py_stream = cStringIO.StringIO(py_input)
    print tokenize.untokenize(tarantool_translate(py_stream.readline))

if __name__ == "__main__":
    main()
