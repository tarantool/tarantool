-- box.uuid
uuid = require('box.uuid')
string.len(uuid.bin())
string.len(uuid.hex())
string.match(uuid.hex(), '^[a-f0-9]+$') ~= nil
