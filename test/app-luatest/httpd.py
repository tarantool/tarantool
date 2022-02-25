#!/usr/bin/env python

import sys
from gevent.pywsgi import WSGIServer
from gevent import spawn, sleep, socket

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
    body = [b'redirecting']
    headers = [("Location", "/")]
    return code, body, headers

paths = {
        "/": hello,
        "/abc": hello1,
        "/absent": absent,
        "/headers": headers,
        "/long_query": long_query,
        "/redirect": redirect,
        }

def read_handle(env, response):
    code = "404 Not Found"
    headers = []
    body = [b'Not Found']
    if env["PATH_INFO"] in paths:
        code, body, headers = paths[env["PATH_INFO"]]()
    for key,value in iter(env.items()):
        if "HTTP_" in key:
            headers.append((key[5:].lower(), value))
    response(code, headers)
    return body

def post_handle(env, response):
    code = "200 OK"
    body = [env["wsgi.input"].read()]
    headers = []
    for key,value in iter(env.items()):
        if "HTTP_" in key:
            headers.append((key[5:].lower(), value))
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

def heartbeat():
    try:
        while True:
            sys.stdout.write("heartbeat\n")
            sys.stdout.flush()
            sleep(1e-1)
    except IOError:
        sys.exit(1)

def usage():
    message = "Usage: {} {{ --inet HOST:PORT | --unix PATH }}\n".format(sys.argv[0])
    sys.stderr.write(message)
    sys.exit(1)

if len(sys.argv) != 3:
    usage()

if sys.argv[1] == "--inet":
    host, port = sys.argv[2].split(":")
    sock_family = socket.AF_INET
    sock_addr = (host, int(port))
elif sys.argv[1] == "--unix":
    path = sys.argv[2]
    sock_family = socket.AF_UNIX
    sock_addr = path
else:
    usage()

sock = socket.socket(sock_family, socket.SOCK_STREAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(sock_addr)
sock.listen(10)

server = WSGIServer(sock, handle, log=None)
spawn(heartbeat)
server.serve_forever()
