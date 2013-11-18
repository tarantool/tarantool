-- box.uuid()
string.len(box.uuid())
string.len(box.uuid_hex())
string.match(box.uuid_hex(), '^[a-f0-9]+$') ~= nil
