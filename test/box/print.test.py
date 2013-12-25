import tarantool

import sys
import os
import re


admin('print("Hello, world")')

log = os.path.join(vardir, "tarantool.log")
f = open(log, "r")
f.seek(0, 2)

admin('box.fiber.wrap(function() print("Hello, world") end)')
admin('box.fiber.sleep(0.1)')
line = f.readline()
print("Check log line")
print("---")
if re.search('(Hello)', line).start(1) > 0:
    print("""- "line contains 'Hello'" """)
    print("...")

admin('box.fiber.wrap(function() print("Ehllo, world") end)')
admin('box.fiber.sleep(0.1)')
line = f.readline()
print("Check log line")
print("---")
if re.search('(Hello)', line):
    print("""- "line contains 'Hello'" """)
else:
    print("""- "line doesn't contain 'Hello'" """)
print("...")

admin('box.fiber.wrap(function() print() end)')
admin('box.fiber.sleep(0.1)')
line = f.readline()
print("Check log line")
print("---")
if re.search('(PPPPPPPP)', line):
    print("""- "line contains 'PPPPPPPP'" """)
else:
    print("""- "line doesn't contain 'PPPPPPPP'" """)
print("...")
