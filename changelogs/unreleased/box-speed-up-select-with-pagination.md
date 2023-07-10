## feature/box

* Sped up `index.select` and `index.pairs` with the `after` option by up to 30%
  in a synthetic test by eliminating an extra buffer allocation.
