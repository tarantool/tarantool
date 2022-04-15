## feature/lua/datetime

 * Datetime interval support has been reimplemented in C
   to make possible future Olson/tzdata and SQL extensions (gh-6923);

All components of interval values now kept and operated separately
(i.e. years, months, weeks, days, hours, seconds and nanoseconds) this
allows to correctly apply date/time arithmetic at the moment we add/subtract
intervals to datetime values.
