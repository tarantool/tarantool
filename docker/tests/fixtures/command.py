#!/usr/bin/python3
import json
import os
from pathlib import Path
import sys

command = Path(sys.argv[0]).name
if command == 'id':
    print(os.environ.get('FAKE_UID', '1000'))
elif command == 'gosu':
    os.environ.update(FAKE_UID='1000', DROPPED_PRIVILEGES='yes')
    os.execv(sys.argv[2], sys.argv[2:])
else:
    Path(os.environ['CAPTURE_FILE']).write_text(json.dumps({
        'argv': sys.argv[1:], 'env': dict(os.environ),
    }))
