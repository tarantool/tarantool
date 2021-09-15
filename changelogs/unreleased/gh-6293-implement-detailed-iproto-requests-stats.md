## feature/core

 * Add new  metrics `REQUESTS_IN_PROGRESS` and `REQUESTS_IN_STREAM_QUEUE`
   to `box.stat.net`, which contain detailed statistics for iproto requests.
   These metrics contains same counters as other metrics in `box.stat.net`:
   current, rps and total (gh-6293).