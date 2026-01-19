## bugfix/sql

Fixed a failing inner join query, projecting parameters from the inner
table to the result projection. As a side effect, changed the default
type of a metadata column from `boolean` to `any`.
