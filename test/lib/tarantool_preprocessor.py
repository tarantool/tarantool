import codecs
import tokenize
import cStringIO
import encodings
import pdb
import re
from encodings import utf_8
import sys

# tarantool operators
TARANTOOL_OPERATORS = [ 'exec', 'send', 'recv' ]

TARANTOOL_METHODS = {
        'exec' : 'execute',
        'send' : 'send',
        'recv' : 'recv'
}

def tarantool_translate(readline):
    token_stream = tokenize.generate_tokens(readline)
    for token in token_stream:
        token_buffer = [ token ]
        # chec token type
        if token_is_operator(token):
            # translate tarantool operator
            translate_command(token_buffer, token_stream)

        while len(token_buffer) > 0:
            yield token_buffer.pop(0)


def translate_command(token_buffer, token_stream):
    operator = token_buffer.pop(0)
    object = next(token_stream)
    if token_is_identifier(object):
        # translate operator
        translate_operator(token_buffer, operator, object)
        translate_operands(token_buffer, operator, token_stream)
    else:
        token_buffer.append(operator)
        token_buffer.append(object)


def translate_operator(token_buffer, operator, object):
    # operator object -> object.method
    # put object
    token_buffer.append(object[:2] + operator[2:])
    # put comma
    token_buffer.append((tokenize.OP, '.') + operator[2:])
    # put method
    operator_name = operator[1]
    method_name = TARANTOOL_METHODS[operator_name]
    token_buffer.append((tokenize.NAME, method_name) + operator[2:])


def translate_operands(token_buffer, operator, token_stream):
    # put open bracket
    token_buffer.append((tokenize.OP, '(') + operator[2:])

    # put all operatands
    token = next(token_stream)

    silent = False
    if token_is_modifier(token):
        silent = modifier_to_value(token[1])
        token = next(token_stream)

    comma_needed = False
    while not token_is_separator(token):
        token_buffer.append(token[:2] + operator[2:])
        comma_needed = True
        token = next(token_stream)

    # set verbose flag
    if comma_needed:
        # we have operatands, put comma before silent
        token_buffer.append((tokenize.OP, ',') + operator[2:])
    token_buffer.append((tokenize.NAME, 'silent') + operator[2:])
    token_buffer.append((tokenize.OP, '=') + operator[2:])
    token_buffer.append((tokenize.NAME, '%s' % silent) + operator[2:])

    # put close bracket
    token_buffer.append((tokenize.OP, ')') + operator[2:])
    # new line
    token_buffer.append((tokenize.NEWLINE, '\n') + operator[2:])


def modifier_to_value(name):
    if name == 'silent':
        return True
    return False


def token_is_modifier(token):
    token_type, token_name = token[:2]
    if token_type == tokenize.NAME and token_name in [ 'silent' , 'verbose' ]:
        return True
    return False


def token_is_operator(token):
    token_type, token_name = token[:2]
    if token_type == tokenize.NAME and token_name in TARANTOOL_OPERATORS:
        return True
    return False


def token_is_identifier(token):
    return token[0] == tokenize.NAME


def token_is_separator(token):
    token_type = token[0]
    if token_type == tokenize.NEWLINE or token_type == tokenize.ENDMARKER:
        return True
    return False


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
