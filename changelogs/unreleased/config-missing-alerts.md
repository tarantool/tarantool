## bugfix/config

* Fixed a bug when only one alert of a particular type is reported in
  `config:info().alerts` instead of several ones. Examples of such alerts are
  privilege grant delay due to a lack of a space/function/sequence and skipping
  of a non-dynamic `box.cfg()` option applies on the configuration reloading
  (gh-9586).
