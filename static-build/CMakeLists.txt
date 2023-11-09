cmake_minimum_required(VERSION 3.1)

# Detect system compilers for further dependencies configuring to be
# built with these compilers. This is used to build tarantool and
# it's dependencies by using one compiler system (for example libicu
# by default uses clang if it exists when others uses gcc/g++ on
# linux machine).
project(tarantool-static C CXX)

include(cmake/AddTarantoolProject.cmake)
