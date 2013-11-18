import subprocess
import sys
import os

admin("box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')")
admin("box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'str')")

p = subprocess.Popen([ os.path.join(builddir, "test/connector_c/update") ], stdout=subprocess.PIPE)
p.wait()
for line in p.stdout.readlines():
      sys.stdout.write(line)

# resore default suite
#server.stop()
#server.deploy(self.suite_ini["config"])
#server.start()

admin("box.space[0]:drop()")

# vim: syntax=python
