CC ?= gcc
RAGEL ?= ragel
DOT ?= dot
CONFETTI ?= confetti

HAVE_GIT ?= 1

CFLAGS += -Wall -Wextra -Werror -Wno-sign-compare -Wno-strict-aliasing \
	  -fno-stack-protector -std=gnu99 \
	  -pipe

ifndef OS
  OS=$(shell uname)
  ARCH=$(shell uname -m | grep -q 'x86\|i[3456]86' && echo x86)
endif

ifeq ($(ARCH), x86)
  CORO_IMPL ?= ASM
else
  CORO_IMPL ?= SJLJ
endif

ifeq ($(OS), FreeBSD)
  CFLAGS += -DFreeBSD -DEV_USE_KQUEUE -DHAVE_SETPROCTITLE
endif

ifeq ($(OS), Linux)
  CFLAGS += -DLinux -D_FILE_OFFSET_BITS=64  -DEV_USE_INOTIFY -D_GNU_SOURCE
  ifeq ($(DEBUG), 2)
    CFLAGS += -DHAVE_VALGRIND
  endif
endif

ifeq (,$(filter -g%,$(CFLAGS)))
CFLAGS += -g3 -ggdb
endif

ifdef DEBUG
  CFLAGS += -DDEBUG -fno-omit-frame-pointer
else
 CFLAGS += -DNDEBUG
endif

ifeq (,$(filter -O%,$(CFLAGS)))
  ifeq ($(DEBUG), 2)
    CFLAGS += -O0
  else
    CFLAGS += -O2
  endif
endif
