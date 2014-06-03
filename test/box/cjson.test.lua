-- cjson tests
json = require('json')
type(json)

json.encode(123)
json.encode({123})
json.encode({123, 234, 345})
json.encode({abc = 234, cde = 345})
json.encode({Метапеременная = { 'Метазначение' } })

json.decode('123')
json.decode('[123, \"Кудыкины горы\"]')[2]
json.decode('{\"test\": \"Результат\"}').test
-- parser test to recognize binary stream
'\83\149\1\11'
