#!/usr/bin/env python

import sys
import tempfile
from gevent.pywsgi import WSGIServer
from gevent import spawn, sleep, socket
from signal import signal, SIGPIPE, SIG_DFL

signal(SIGPIPE, SIG_DFL)

def absent():
    code = "500 Server Error"
    headers = [("Content-Type", "application/json")]
    body = [b'No such method']
    return code, body, headers

def hello():
    code = "200 OK"
    body = [b'hello world']
    headers = [("Content-Type", "application/json")]
    return code, body, headers

def hello1():
    code = "200 OK"
    body = [b'abc']
    headers = [("Content-Type", "application/json")]
    return code, body, headers

def headers():
    code = "200 OK"
    body = [b'cookies']
    headers = [("Content-Type", "application/json"),
               ("Content-Type", "application/yaml"),
               ("Set-Cookie", "likes=cheese; Expires=Wed, 21 Oct 2015 07:28:00 GMT; Secure; HttpOnly"),
               ("Set-Cookie", "bad@name=no;"),
               ("Set-Cookie", "badcookie"),
               ("Set-Cookie", "good_name=yes;"),
               ("Set-Cookie", "age = 17; NOSuchOption; EmptyOption=Value;Secure"),
               ("my_header", "value1"),
               ("my_header", "value2"),
               ("very_very_very_long_headers_name1", "true"),
               ]
    return code, body, headers

def long_query():
    sleep(0.005)
    code = "200 OK"
    body = [b'abc']
    headers = [("Content-Type", "application/json")]
    return code, body, headers

def redirect():
    code = "302 Found"
    body = [b'redirecting...']
    headers = [("Location", "/")]
    return code, body, headers

def lango_body():
    code = "200 OK"
    body = [b'lango']
    headers = [("Content-Type", "application/lango")]
    return code, body, headers

def json_body():
    code = "200 OK"
    body = [b'[1,2]']
    headers = [("Content-Type", "application/json; charset=utf-8")]
    return code, body, headers

read_paths = {
        "/": hello,
        "/abc": hello1,
        "/absent": absent,
        "/headers": headers,
        "/long_query": long_query,
        "/redirect": redirect,
        "/json_body": json_body,
        "/lango_body": lango_body,
        }

def encoding(env):
    code = "200 OK"
    if "HTTP_TRANSFER_ENCODING" in env:
        body = [str.encode(env["HTTP_TRANSFER_ENCODING"])]
    else:
        body = [b'none']
    headers = []
    return code, body, headers

post_paths = {
        "/encoding": encoding,
        }

def read_handle(env, response):
    code = "404 Not Found"
    headers = []
    body = [b'Not Found']
    if env["PATH_INFO"] in read_paths:
        code, body, headers = read_paths[env["PATH_INFO"]]()
    for key,value in iter(env.items()):
        if "HTTP_" in key:
            headers.append((key[5:].lower(), value))
    response(code, headers)
    return body

def post_handle(env, response):
    code = "200 OK"
    body = [env["wsgi.input"].read()]
    headers = []
    if env["PATH_INFO"] in post_paths:
        code, body, headers = post_paths[env["PATH_INFO"]](env)
    for key,value in iter(env.items()):
        if "HTTP_" in key:
            headers.append((key[5:].lower(), value))
    if env.get("CONTENT_TYPE"):
        headers.append(("content_type", env["CONTENT_TYPE"]))
    response(code, headers)
    return body

def other_handle(env, response, method, code):
    headers = [("Content-Type", "text/plain"), ("method", method)]
    body = [method.encode('utf-8')]
    for key,value in iter(env.items()):
        if "HTTP_" in key:
            headers.append((key[5:].lower(), value))
    response(code, headers)
    return body

OTHER_METHODS = {
    "TRACE": True,
    "CONNECT": True,
    "OPTIONS": True,
    "DELETE": True ,
    "HEAD": True
}

def handle(env, response) :
    method = env["REQUEST_METHOD"].upper()
    if method == "GET":
        return read_handle(env, response)
    elif method == "PUT" or method == "POST" or method == "PATCH":
        return post_handle(env, response)
    elif method in OTHER_METHODS:
        return other_handle(env, response, method, "200 Ok")
    return other_handle(env, response, method, "400 Bad Request")

def heartbeat(sockname):
    sockname_str = sockname
    if type(sockname) == tuple:
        sockname_str = "{}:{}".format(sockname[0], sockname[1])
    try:
        while True:
            sys.stdout.write(sockname_str + "\n")
            sys.stdout.flush()
            sleep(1e-1)
    except IOError:
        sys.exit(1)

def usage():
    message = "Usage: {} {{ --conn-type AF_INET | AF_UNIX }}\n".format(sys.argv[0])
    sys.stderr.write(message)
    sys.exit(1)

if len(sys.argv) != 3:
    usage()

if sys.argv[1] == "--conn-type":
    conn_type = sys.argv[2]
    if conn_type == "AF_UNIX":
        sock_family = socket.AF_UNIX
        td = tempfile.TemporaryDirectory()
        sock_addr = "{}/httpd.sock".format(td.name)
    elif conn_type == "AF_INET":
        sock_family = socket.AF_INET
        sock_addr = ("127.0.0.1", 0)
else:
    usage()

sock = socket.socket(sock_family, socket.SOCK_STREAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(sock_addr)
sock.listen(10)

server = WSGIServer(sock, handle, log=None)
spawn(heartbeat, sock.getsockname())
server.serve_forever()
