## bugfix/box

* Fixed `box.schema.user.passwd(password)` changing the password of the
  session user instead of the effective user when called from
  `box.session.su(user, func)` (gh-13243).
