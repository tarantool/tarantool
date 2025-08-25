## bugfix/config

* Evaluate configurations for other cluster members lazily to speed up startup
  and reload of large configurations (500 instances or more).
