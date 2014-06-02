-- box.uuid
uuid = require('uuid')
string.len(uuid.bin())
string.len(uuid.hex())
string.match(uuid.hex(), '^[a-f0-9]+$') ~= nil
uuid = nil
