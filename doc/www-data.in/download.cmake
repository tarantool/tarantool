{% page download en %}

### What's in the package

Each binary package includes the server binary, *tarantool_box*,
the command line client, *tarantool*, instance management
script, *tarantool.sh*, a script to add more instances,
*tarantool_expand.sh*, and, last but not least, man pages and the user
guide.

### How to choose the right version for download 

Tarantool/Box uses a 3-digit versioning scheme
&lt;major&gt;-&lt;minor&gt;-&lt;patch&gt;.
Major digits change rarely. A minor version increase
indicates an incompatibile change. Minor version change happens
when the source tree receives a few important bugfixes.

The version string may also contain a git revision id, to ease
identification of the unqiue commit used to generate the build.

The current version of the stable branch is **@TARANTOOL_VERSION@**.

An automatic build system creates, tests and publishes packages
for every push into the stable branch. All binary packages contain
symbol information. Additionally, **-debug-** 
packages contain asserts and are compiled without optimization.

To simplify problem analysis and avoid various bugs induced 
by compilation parameters and environment, it is recommended
that production systems use the builds provided on this site.

The latest build can be found below.

<table border=1 title="Download the latest build, @TARANTOOL_VERSION@" width=100%> 

  <tr width=60%>
    <td colspan=1>Source tarball</td>
    <td colspan=3 align=center>
        <a href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@.src.tar.gz">tarantool-@TARANTOOL_VERSION@.src.tar.gz</a>
    </td>
  </tr>

  <th colspan=3>Linux</th>

<!-- Debian -->

  <tr>
    <td>
        Debian software package (<b>.deb</b>)
    </td>

    <td align=center>
        <a href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-i686.deb">32-bit</a>
    </td>

    <td align=center>
        <a href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-i686.deb">64-bit</a>
    </td>
  </tr>

  <tr>
    <td>
        Debian software package (<b>.deb</b>), with debug info 
    </td>
    <td align=center>
        <a
        href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-i686-debug.deb">32-bit</a>
    </td>

    <td align=center>
        <a
        href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-x864_64-debug.deb">64-bit</a>
    </td>
  </tr>

<!-- RPM -->

  <tr>
    <td>
        RedHat <b>.rpm</b>
    </td>

    <td align=center>
        <a href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-i686.rpm">32-bit</a>
    </td>

    <td align=center>
        <a href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-i686.rpm">64-bit</a>
    </td>
  </tr>

  <tr>
    <td>
        RedHat <b>.rpm</b>, with debug info
    </td>
    <td align=center>
        <a
        href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-i686-debug.rpm">32-bit</a>
    </td>

    <td align=center>
        <a
        href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-x864_64-debug.rpm">64-bit</a>
    </td>
  </tr>

<!-- .tar.gz -->

  <tr>
    <td>
        Binary tarball (<b>.tar.gz</b>) 
    </td>
    <td align=center>
        <a href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-i686.tar.gz">32-bit</a>
    </td>

    <td align=center>
        <a href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-x86_64.tar.gz">64-bit</a>
    </td>
  </tr>

  <th colspan=3>FreeBSD</th>

<!-- .tar.gz -->

  <tr>
    <td>
        Binary tarball (<b>.tar.gz</b>) 
    </td>
    <td align=center>
        <a
        href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-freebsd-i386.tar.gz">32-bit</a>
    </td>

    <td align=center>
    </td>
  </tr>
  <th colspan=4>Mac OS X</th>
  <tr> 
    <td align=center colspan=4><i>Coming soon...</i></td>
  <tr>

</table>

### All downloads

An archive of old releases can be found at <a
href="http://tarantool.org/dist">here</a>.

### Connectors

- Perl driver, [CPAN home](http://search.cpan.org/~yuran/MR-Tarantool/)
- [Ruby driver](https://github.com/mailru/tarantool-ruby)
- Python driver, [hosted at pypi.python.org](http://pypi.python.org/pypi/tarantool)
{% page download ru %}

<!-- vim: syntax=html
-->
