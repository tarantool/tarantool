{% page download en %}

### How to choose the right version for download 

Tarantool/Box uses a 3-digit versioning scheme
&lt;major&gt;-&lt;minor&gt;-&lt;patch&gt;.
Major digits change rarely. A minor version increase indicates one
or few incompatibile changes. Patch verison counter is increased
whenever the source tree receives a few important bugfixes.

The version string may also contain a git revision id, to ease
identification of the unqiue commit used to generate the build.

The current version of the stable branch is **@TARANTOOL_VERSION@**.

An automatic build system creates, tests and publishes packages
for every push into the stable branch. All binary packages contain
symbol information. Additionally, **-debug-** 
packages contain asserts and are compiled without optimization.

#### Source tarball

The latest source archive is
<a href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-src.tar.gz">tarantool-@TARANTOOL_VERSION@-src.tar.gz</a>.
Please consult with README for build instructions on your system.

### Binary downloads

To simplify problem analysis and avoid various bugs induced 
by compilation parameters and environment, it is recommended
that production systems use the builds provided on this site.

#### RedHat, FreeBSD, Mac OS X

<table border=1 title="Download the latest build, @TARANTOOL_VERSION@" width=100%> 

<!-- RPM -->

  <th colspan=3>RedHat</th>

  <tr>
    <td>
        RedHat <b>.rpm</b>
    </td>

    <td align=center>
        <a href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-i686.rpm">32-bit</a>
    </td>

    <td align=center>
        <a
        href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-x86_64.rpm">64-bit</a>
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
        href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-x86_64-debug.rpm">64-bit</a>
    </td>
  </tr>

<!-- .tar.gz -->
  <th colspan=3>Other Linux distributions</th>

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
    <td align=center colspan=4>brew install http://tarantool.org/dist/tarantool.rb</td>
  <tr>

</table>

#### Debian GNU/Linux and Ubuntu

We maintain an always up-to-date Debian GNU/Linux and Ubuntu package
repository at <a
href="http://tarantool.org/dist/debian">http://tarantoo.org/dist/debian</a> and <a
href="http://tarantool.org/dist/ubuntu">http://tarantool.org/dist/ubuntu</a> respectively.

At the moment the repository contains builds for Debian "Sid", "Squeeze",
"Wheezy" and Ubuntu "Precise". It can be added to your apt sources list with:

    wget http://tarantool.org/dist/public.key
    sudo apt-key add ./public.key
    release=`lsb_release -c -s`
    echo "deb http://tarantool.org/dist/debian/ $release main" | sudo tee -a /etc/apt/sources.list.d/tarantool.list
    echo "deb-src http://tarantool.org/dist/debian/ $release main" | sudo tee -a /etc/apt/sources.list.d/tarantool.list
    sudo apt-get update
    sudo apt-get install tarantool tarantool-client

#### All downloads

An archive of old releases can be found at <a
href="http://tarantool.org/dist">http://tarantool.org/dist</a>.

#### Connectors

- Perl driver, [CPAN home](http://search.cpan.org/~yuran/MR-Tarantool/)
- [Ruby driver](https://github.com/mailru/tarantool-ruby)
- Python driver, [hosted at pypi.python.org](http://pypi.python.org/pypi/tarantool)

{% page download ru %}

### Как правильно выбрать версию сервера

Tarantool/Box использует стандартную схему нумерования
релизов &lt;major&gt;-&lt;minor&gt;-&lt;patch&gt;.

Первая цифра в версии изменяется крайне редко, по итогам
накопления большого количества новых возможностей.
Изменение средней цифры является индикатором несовместимых
изменений в сервере, таких как изменение протокола, удаление
опций командной строки или конфигурационного файла.
Последняя цифра увеличивается при создании очередного 
релиза, направленного на устранение обнаруженных ошибок.

В конце строки версии также может присутствовать git commit id, 
что упрощает определение конкретной версии репозитория, использованной
для создания релиза.

Текущая стабильная версия **@TARANTOOL_VERSION@**.

#### Исходный код 

Архив .tar.gz с последний версией исходного кода доступен по следующей
ссылке: 
<a href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-src.tar.gz">tarantool-@TARANTOOL_VERSION@-src.tar.gz</a>.
Файл README.<OS> содержит инструкции по сборке из исходников.

### Бинарные пакеты 

Для использования в высоконагруженных проектах
рекомендуются пакеты, опубликованные на этом сайте.

#### RedHat, FreeBSD, Mac OS X

<table border=1 title="Download the latest build, @TARANTOOL_VERSION@" width=100%> 

<!-- RPM -->

  <th colspan=3>RedHat</th>

  <tr>
    <td>
        RedHat <b>.rpm</b>
    </td>

    <td align=center>
        <a href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-i686.rpm">32-bit</a>
    </td>

    <td align=center>
        <a
        href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-x86_64.rpm">64-bit</a>
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
        href="http://tarantool.org/dist/tarantool-@TARANTOOL_VERSION@-linux-x86_64-debug.rpm">64-bit</a>
    </td>
  </tr>

<!-- .tar.gz -->
  <th colspan=3>Other Linux distributions</th>

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
    <td align=center colspan=4>brew install http://tarantool.org/dist/tarantool.rb</td>
  <tr>

</table>

#### Debian GNU/Linux and Ubuntu

Мы поддерживаем собственные репозитории .deb пакетов, который
содержит последние версии сервера и клиентских библиотек для 
*Debian GNU/Linux* и  *Ubuntu*:
<a href="http://tarantool.org/dist/debian">http://tarantoo.org/dist/debian</a>,
<a href="http://tarantool.org/dist/ubuntu">http://tarantool.org/dist/ubuntu</a>.

Репозитории содержат пакеты для Debian "Sid", "Squeeze",
"Wheezy" и Ubuntu "Precise". Для установки можно воспользоваться следующим
скриптом:

    wget http://tarantool.org/dist/public.key
    sudo apt-key add ./public.key
    release=`lsb_release -c -s`
    echo "deb http://tarantool.org/dist/debian/ $release main" | sudo tee -a /etc/apt/sources.list.d/tarantool.list
    echo "deb-src http://tarantool.org/dist/debian/ $release main" | sudo tee -a /etc/apt/sources.list.d/tarantool.list
    sudo apt-get update
    sudo apt-get install tarantool tarantool-client

### Архив старых релизов

Архив старых релизов доступен по адресу <a
href="http://tarantool.org/dist">http://tarantool.org/dist</a>.

### Доступ из языков программирования

- Perl driver, [CPAN home](http://search.cpan.org/~yuran/MR-Tarantool/)
- [Ruby driver](https://github.com/mailru/tarantool-ruby)
- Python driver, [hosted at pypi.python.org](http://pypi.python.org/pypi/tarantool)
