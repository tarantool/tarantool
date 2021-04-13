## feature/core

* Implement ability to run multiple iproto threads, which is useful
  in some specific workloads where iproto thread is the bottleneck
  of throughput (gh-5645).