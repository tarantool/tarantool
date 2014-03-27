import tarantool

import sys
import os
import re


admin('print("Hello, world")')
admin("io = require('io')")

log = server.logfile
f = open(log, "r")
f.seek(0, 2)

admin('box.fiber.wrap(function() print("Ehllo, world") io.flush() end)')
admin('box.fiber.sleep(0.1)')
line = f.readline()
print("Check log line")
print("---")
found = re.search(r'(Hello)', line)
if found and re.search(r'(Hello)', line).start(1) >= 0:
    print("""- "line contains 'Hello'" """)
    print("...")
else:
    print('String "%s" does not contain "Hello"' % line)

line = f.readline()
print("Check log line")
print("---")
if re.search('(Ehllo)', line):
    print("""- "line contains 'Ehllo'" """)
else:
    print("""- "line doesn't contain 'Ehllo'" """)
print("...")

