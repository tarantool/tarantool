import tarantool

import sys
import os
import re

log = server.get_log()

admin('print("Hello, world")')
admin("io = require('io')")

admin("""local f = require('fiber').create(
    function()
        print('Ehllo, world')
        io.flush()
    end
)""")
admin("require('fiber').sleep(0.1)")

print("Check log line (Hello):")
print('---')
if log.seek_once('Hello') >= 0:
    print('- "logfile contains "Hello""')
else:
    print('- "logfile does not contain "Hello""')
print('...')

print("Check log line (Ehllo):")
print('---')
if log.seek_once('Ehllo') >= 0:
    print('- "logfile contains "Ehllo""')
else:
    print('- "logfile does not contain "Ehllo""')
print('...')
