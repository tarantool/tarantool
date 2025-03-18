## feature/config
* Now the `stop` callback for the roles is called during graceful shutdown. The
  `stop` callbacks are called in the reverse order of roles startup (gh-10795).
