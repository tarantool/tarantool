## feature/sql

* **[Breaking change]** Names in SQL are now case sensitive. To support backward
  compatibility, a second lookup using a name normalized using the old rules is
  added (gh-4467).
