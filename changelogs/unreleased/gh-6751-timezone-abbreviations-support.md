## feature/lua/datetime

 * Extend datetime literal parser with ability to handle known timezone
   abbreviations (i.e. 'MSK', 'CET', etc.) which are deterministically
   translated to their offzet (gh-5941, gh-6751);

One may use timezone abbreviations in addition to the timezone offset in
the datetime literals, e.g. these literals produce equivalent datetime
values:

  local date = require('datetime')
  local d1 = date.parse('2000-01-01T02:00:00+0300')
  local d2 = date.parse('2000-01-01T02:00:00 MSK')
  local d3 = date.parse('2000-01-01T02:00:00 MSK', {format = '%FT%T %Z'})

Parser fails if one uses ambiguous names (e.g. 'AT') which could not be
directly translated into timezone offsets.
