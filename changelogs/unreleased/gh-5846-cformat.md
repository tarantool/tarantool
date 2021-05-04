## bugfix/core
 * Fixed wrong type specification when printing fiber state
   change which lead to negative fiber's ID logging (gh-5846).

   For example
   ```
   main/-244760339/cartridge.failover.task I> Instance state changed
   ```
   instead of proper
   ```
   main/4050206957/cartridge.failover.task I> Instance state changed
   ```
