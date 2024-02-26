# Tarantool

[![Actions Status][actions-badge]][actions-url]
[![Code Coverage][coverage-badge]][coverage-url]
[![OSS Fuzz][oss-fuzz-badge]][oss-fuzz-url]
[![Telegram][telegram-badge]][telegram-url]
[![GitHub Discussions][discussions-badge]][discussions-url]
[![Stack Overflow][stackoverflow-badge]][stackoverflow-url]

Переводы:

- [English](https://github.com/tarantool/tarantool/blob/master/README.md)

[Tarantool][tarantool-url] это вычислительная in-memory платформа, состоящая из базы данных и сервера приложения.

Распространяется на условиях [BSD 2-Clause][license].

Ключевые преимущества сервера приложения:

* Значительно оптимизированный интерпретатор Lua с невероятно быстрым трассирующим JIT-компилятором
  на основе LuaJIT 2.1.
* Кооперативная многозадачность, неблокирующий ввод-вывод.
* [Персистентные очереди][queue].
* [Шардирование][vshard].
* [Фреймворк кластера и веб приложения][cartridge].
* Доступ к внешним базам данных, таким как [MySQL][mysql] и [PostgreSQL][pg].
* Богатый набор встроенных и автономных [модулей][modules].

Ключевые преимущества базы данных:

* Формат данных MessagePack и клиент-сервер протокол на основе MessagePack.
* Два способа хранения данных: 100% в оперативной памяти с полным сохранением на основе WAL и
  с собственной реализацией LSM-дерева, чтобы использовать большие дата сеты.
* Несколько типов индексов: HASH, TREE, RTREE, BITSET.
* Документоориентированные индексы путей JSON.
* Ассинхронная master-master репликация.
* Синхронная quorum-based репликация.
* Автоматические выборы лидера на основе RAFT для конфигурации с одним лидером.
* Аутентификация и контроль доступа.
* ANSI SQL, включая представления, объединения, ссылочные и проверочные ограничения.
* [Коннекторы][connectors] для множества языков програмирования.
* База данных представляет собой расширение C сервера приложений и может быть выключена.

Поддерживаемые платформы: Linux (x86_64, aarch64), Mac OS X (x86_64, M1), FreeBSD
(x86_64).

Tarantool идеально подходит для компонентов масштабируемой веб-архитектуры:
серверы очередей, кэши, веб-приложения с отслеживанием состояния.

Чтобы установить и скачать Tarantool как пакет для вашей операционной системы или использования
Docker, пожалуйста ознакомьтесь с  [иструкциями по скачиванию][download].

Чтобы собрать Tarantool из исходных файлов, ознакомьтесь с [инструкциями][building] в
документации Tarantool.

Чтобы найти модули, коннекторы и инструменты для Tarantool, ознакомьтесь со списком [Awesome
Tarantool][awesome-list].

Пожалуйста, сообщайте о багах в наш [issue tracker][issue-tracker]. Мы также тепло
привествуем ваш фидбек в странице [дисскуссий][discussions-url] и вопросов
на [Stack Overflow][stackoverflow-url].

Мы принимаем контрибьютинг с помощью пул реквестов. 
Ознакомьтесь с нашим [гайдом контрибьютинга][contributing].

Благодарим вас за интерес к Tarantool!

[actions-badge]: https://github.com/tarantool/tarantool/workflows/release/badge.svg

[actions-url]: https://github.com/tarantool/tarantool/actions

[coverage-badge]: https://coveralls.io/repos/github/tarantool/tarantool/badge.svg?branch=master

[coverage-url]: https://coveralls.io/github/tarantool/tarantool?branch=master

[telegram-badge]: https://img.shields.io/badge/Telegram-join%20chat-blue.svg

[telegram-url]: http://telegram.me/tarantool

[discussions-badge]: https://img.shields.io/github/discussions/tarantool/tarantool

[discussions-url]: https://github.com/tarantool/tarantool/discussions

[stackoverflow-badge]: https://img.shields.io/badge/stackoverflow-tarantool-orange.svg

[stackoverflow-url]: https://stackoverflow.com/questions/tagged/tarantool

[oss-fuzz-badge]: https://oss-fuzz-build-logs.storage.googleapis.com/badges/tarantool.svg

[oss-fuzz-url]: https://oss-fuzz.com/coverage-report/job/libfuzzer_asan_tarantool/latest

[tarantool-url]: https://www.tarantool.io/en/

[license]: LICENSE

[modules]: https://www.tarantool.io/en/download/rocks

[queue]: https://github.com/tarantool/queue

[vshard]: https://github.com/tarantool/vshard

[cartridge]: https://github.com/tarantool/cartridge

[mysql]: https://github.com/tarantool/mysql

[pg]: https://github.com/tarantool/pg

[connectors]: https://www.tarantool.io/en/download/connectors

[download]: https://www.tarantool.io/en/download/

[building]: https://www.tarantool.io/en/doc/latest/dev_guide/building_from_source/

[issue-tracker]: https://github.com/tarantool/tarantool/issues

[contributing]: CONTRIBUTING.md

[awesome-list]: https://github.com/tarantool/awesome-tarantool/
