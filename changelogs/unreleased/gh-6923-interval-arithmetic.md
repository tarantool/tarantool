## feature/lua/datetime

* Datetime interval support has been reimplemented in C to make possible
  future Olson/tzdata and SQL extensions (gh-6923);

  Now all components of the interval values are kept and operated separately
  (i.e. years, months, weeks, days, hours, seconds, and nanoseconds). This
  allows to apply date/time arithmetic correctly when we add/subtract
  intervals to datetime values.
