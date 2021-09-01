## bugfix/core

* Fixed dropping incoming messages when connection is closed or SHUT_RDWR
  received and net_msg_max or readahead limit is reached (gh-6292).
