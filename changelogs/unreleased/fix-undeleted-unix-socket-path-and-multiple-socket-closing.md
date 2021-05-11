## core/bugfix

 * Fixed error, related to the fact, that if user changed listen address,
   all iproto threads closed same socket multiple times.
   Fixed error, related to the fact, that tarantool not deleting the unix
   socket path, when it's finishing work.