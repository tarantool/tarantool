from lib.utils import check_libs
check_libs()
from tarantool.request import RequestSelect

errstr = """---
- error:
    errcode: {0}
    errmsg: {1}
..."""

def format_error(response):
    return errstr.format(
        "(%d)" % response.return_code,
        response.return_message)

def format_yamllike(response):
    table = ("\n"+"\n".join(["- "+str(list(k)) for k in response])) \
            if len(response) else ""
    return "---{0}\n...".format(table)

def select(conn, space_no, index_no, key, offset=0, limit=0, iterator=0):
    data = RequestSelect(
        conn, space_no, index_no, 
        key, offset, limit, iterator
    )
    response = conn._send_request(data)

    if response.return_code:
        return format_error(response)
    return format_yamllike(response)

print """#
# A test case for: http://bugs.launchpad.net/bugs/712456
# Verify that when trying to access a non-existing or
# very large space id, no crash occurs.
#
"""
print select(iproto.py_con, 1, 0, [0])
print select(iproto.py_con, 65537, 0, [0])
print select(iproto.py_con, 4294967295, 0, [0])

print """#
# A test case for: http://bugs.launchpad.net/bugs/716683
# Admin console should not stall on unknown command.
"""
admin("show status", simple=True)
