## bugfix/datetime

 * Intervals received after datetime arithmetic operations may be improperly
   normalized if result was negative

```
   tarantool> date.now() - date.now()
---
- -1.000026000 seconds
...
```
   i.e. 2 immediately called `date.now()` produce very close values, whose
   difference should be close to 0, not 1 second. (gh-6882);
