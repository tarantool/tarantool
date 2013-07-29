import subprocess
import sys
import os

p = subprocess.Popen([ os.path.join(builddir, "test/connector_c/update") ], stdout=subprocess.PIPE)
p.wait()
for line in p.stdout.readlines():
      sys.stdout.write(line)

# resore default suite
server.stop()
server.deploy(self.suite_ini["config"])
server.start()
# vim: syntax=python
