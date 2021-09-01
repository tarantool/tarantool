## feature/core

 * Add new  metric `STREAMS` to `box.stat.net`, which contain statistics
   for iproto streams. `STREAMS` contains same counters as 'CONNECTIONS'
   metric in `box.stat.net`: current, rps and total (gh-6293).