## feature/core

*  Update operations can't insert with gaps. This patch changes
   the behavior so that the update operation fills the missing
   fields with nulls (gh-3378).