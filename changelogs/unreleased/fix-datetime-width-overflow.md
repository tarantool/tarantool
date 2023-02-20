## bugfix/datetime

* Fixed a segmentation fault that happened when the value passed to the ``%f``
  modifier of ``datetime_object:format()`` was too big (ghs-31).
