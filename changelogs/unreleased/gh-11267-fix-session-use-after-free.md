## bugfix/box

* Fixed use-after-free for session created implicitly when using
  `tnt_tx_push()` to execute code making access checks (gh-11267).
