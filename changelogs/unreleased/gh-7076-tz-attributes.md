## feature/lua/datetime

* Support for timezone names enabled in constructor and `date:set{}` modifier
  via `tz` attribute. At the moment it supports only timezone names
  abbreviations (gh-7076)

Timezone abbreviations can be used in addition to the timezone offset at the
moment of construction or modifying of date object, or while parsing datetime
literals. Numeric time offsets and named abbreviations produce equivalent
datetime values:

```
local date = require('datetime')
local d2 = date.parse('2000-01-01T02:00:00 MSK')

local d1 = date.new{year = 1980, tz = 'MSK'}
d2 = date.new{year = 1980, tzoffset = 180}
d2:set{tz = 'MSK'}
```

Beware, that timezone name parser fails if one uses ambiguous names
(e.g. 'AT') which could not be directly translated into timezone offsets.
