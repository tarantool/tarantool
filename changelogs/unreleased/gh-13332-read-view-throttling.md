## bugfix/core

* Read view creation is now throttled: opening a non-system read view less
  than 100 ms after the previous one now makes the calling fiber yield until
  the interval elapses. This keeps the internal read view version counter
  from wrapping around (gh-13332).
