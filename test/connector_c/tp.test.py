import subprocess
import sys
import os

p = subprocess.Popen([os.path.join(builddir, "test/connector_c/tp")],
                     stdout=subprocess.PIPE)
o,e = p.communicate()
sys.stdout.write(o)
