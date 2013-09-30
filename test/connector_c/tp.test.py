import subprocess
import sys
import os

admin("box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')")
admin("box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'str')")

p = subprocess.Popen([os.path.join(builddir, "test/connector_c/tp")],
                     stdout=subprocess.PIPE)
o,e = p.communicate()
sys.stdout.write(o)

admin("box.space[0]:drop()")
