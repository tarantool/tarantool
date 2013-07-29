import subprocess
import sys
import os

p = subprocess.Popen([os.path.join(builddir, "test/connector_c/snap"),
		              os.path.abspath("connector_c/connector.snap")],
                     stdout=subprocess.PIPE)
o,e = p.communicate()
sys.stdout.write(o)

# vim: syntax=python
