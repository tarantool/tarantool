## feature/lua/datetime

* Enabled support for timezone names in the constructor and `date:set{}` modifier
  via `tz` attribute. Currently, only timezone name abbreviations
  are supported (gh-7076).

  Timezone abbreviations can be used in addition to the timezone offset.
  They can be used during constructing or modifying a date object, or while
  parsing datetime literals. Numeric time offsets and named abbreviations
  produce equivalent datetime values:

  ```
  local date = require('datetime')
  local d2 = date.parse('2000-01-01T02:00:00 MSK')

  local d1 = date.new{year = 1980, tz = 'MSK'}
  d2 = date.new{year = 1980, tzoffset = 180}
  d2:set{tz = 'MSK'}
  ```

  Note that the timezone name parser fails if one uses ambiguous names,
  which could not be translated into timezone offsets directly (for example, 'AT').
