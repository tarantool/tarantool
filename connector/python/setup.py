# -*- coding: utf-8 -*-
from distutils.core import setup
import os.path

setup(
    name = "tarantool-python",
    packages = ["tarantool"],
    package_dir = {"tarantool": os.path.join("src", "tarantool")},
    version = "0.1.1-dev",
    platforms = ["all"],
    author = "Konstantin Cherkasoff",
    author_email = "k.cherkasoff@gmail.com",
    url = "https://github.com/coxx/tarantool-python",
    description = "Python client library for Tarantool Database",
)
