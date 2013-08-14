-- cjson tests
type(box.cjson)
box.cjson.encode(123)
box.cjson.encode({123})
box.cjson.encode({123, 234, 345})
box.cjson.encode({abc = 234, cde = 345})
box.cjson.encode({Метапеременная = { 'Метазначение' } })

box.cjson.decode('123')
box.cjson.decode('[123, \"Кудыкины горы\"]')[2]
box.cjson.decode('{\"test\": \"Результат\"}').test
-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
