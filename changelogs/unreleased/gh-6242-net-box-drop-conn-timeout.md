## feature/core

* **[Breaking change]** timeout() method of net.box connection, which was
  marked deprecated more than four years ago (in 1.7.4) was dropped, because
  it negatively affected performance of hot net.box methods, like call() and
  select(), in case those are called without specifying a timeout (gh-6242).

---
Breaking change: timeout() method of net.box connection was dropped.
