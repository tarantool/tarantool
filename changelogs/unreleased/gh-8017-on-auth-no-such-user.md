## bugfix/core

* Fixed the bug because of which `box.session.on_auth` triggers were not
  invoked if the authenticated user didn't exist (gh-8017).
