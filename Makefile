
# This makefile based on ideas from http://make.paulandlesley.org/
# Thanks to Paul D. Smith <psmith@gnu.org>

ECHO=/bin/echo
CAT=/bin/cat

# make magick
SUB_MAKE:=$(filter _%,$(notdir $(CURDIR)))
OBJDIR:=$(shell $(ECHO) $(filter _%,$(MAKECMDGOALS)) | tr ' ' '\n' | cut -d/ -f1 | sort | uniq)
ifeq (,$(SUB_MAKE))
ifneq (,$(OBJDIR))
.SUFFIXES:
MAKEFLAGS += -rR --no-print-directory SRCDIR=$(CURDIR) VPATH=$(CURDIR)
FILTERED_MAKECMDGOALS=$(subst $@/,,$(filter $@/%,$(MAKECMDGOALS)))
MODULE=$(subst tarantool_,,$(FILTERED_MAKECMDGOALS))
.PHONY: $(OBJDIR)
$(OBJDIR):
	+@mkdir -p $@
	+@$(MAKE) -C $@ -f $(CURDIR)/Makefile OBJDIR=$@ module=$(MODULE) $(FILTERED_MAKECMDGOALS)

Makefile: ;
%.mk :: ;
% :: $(OBJDIR) ; @:
else
SRCDIR:=$(CURDIR)
endif
endif

# this global rules are always defined
module ?= silverbox
all: tarantool_$(module)

ifeq ("$(origin module)", "command line")
.PHONY: clean
clean:
	@rm -rf $(obj) $(dep) tarantool_$(module) _* lcov test
else
.PHONY: clean
clean:
	@for mod in mod/*; do make --no-print-directory module=`basename $$mod` clean; done
endif
.PHONY: TAGS
tags:
	ctags -R -e -f TAGS

# then SRCDIR is defined, build type is selected and it's time to define build rules
ifneq (,$(SRCDIR))
-include $(SRCDIR)/config.mk $(SRCDIR)/scripts/config.mk
include $(SRCDIR)/scripts/config_def.mk

ifeq (_debug,$(OBJDIR))
  DEBUG=0
else ifeq (_test,$(OBJDIR))
 CFLAGS += --coverage -DCOVERAGE -DNDEBUG
else ifeq (_coverage,$(OBJDIR))
 CFLAGS += --coverage -DCOVERAGE
endif

# build dir includes going first
ifneq (,$(OBJDIR))
CFLAGS += -I$(SRCDIR)/$(OBJDIR) -I$(SRCDIR)/$(OBJDIR)/include
endif
CFLAGS += -I$(SRCDIR) -I$(SRCDIR)/include
LIBS += -lm

subdirs = third_party mod/$(module) cfg core
include $(foreach dir,$(subdirs),$(SRCDIR)/$(dir)/Makefile)

tarantool_$(module): $(obj)
	$(CC) $(CFLAGS) $(LDFLAGS) $(LIBS) $^ -o $@

ifdef I
$(info * build with $(filter -D%,$(CFLAGS)))
endif

# makefile change will force rebuild
$(obj): $(wildcard ../*.mk) $(wildcard ../scripts/*.mk)
$(obj): $(foreach dir,$(subdirs),$(SRCDIR)/$(dir)/Makefile)
$(obj): Makefile

dep = $(patsubst %.o,%.d,$(obj)) $(patsubst %.o,%.pd,$(obj))
-include $(dep)

ifneq (,$(OBJDIR))
%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MD -c $< -o $@
	@sed -n -f $(SRCDIR)/scripts/slurp.sed \
		-f $(SRCDIR)/scripts/fixdep.sed \
		-e 's!$(SRCDIR)/!!; p' \
		< $(@:.o=.d) > $(@:.o=.pd)
else
%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MD -c $< -o $@
endif

# code gen targets
.PRECIOUS: %.h %.c %.dot
ifeq ($(HAVE_RAGEL),1)
%.c: %.rl
	@mkdir -p $(dir $@)
	$(RAGEL) -G2 $< -o $@

%.dot: %.rl
	@mkdir -p $(dir $@)
	$(RAGEL) -p -V $< -o $(basename $@).dot

%.png: %.dot
	@mkdir -p $(dir $@)
	$(DOT) -Tpng $< -o $(basename $@).png
endif
ifeq ($(HAVE_CONFETTI),1)
%.cfg: %.cfg_tmpl
	@mkdir -p $(dir $@)
	$(CONFETTI) -i $< -n tarantool_cfg -f $@

%.h: %.cfg_tmpl
	@mkdir -p $(dir $@)
	$(CONFETTI) -i $< -n tarantool_cfg -h $@

%.c: %.cfg_tmpl
	@mkdir -p $(dir $@)
	$(CONFETTI) -i $< -n tarantool_cfg -c $@
endif

ifeq ($(HAVE_GIT),1)
tarantool_version.h: FORCE
	@echo -n "const char tarantool_version_string[] = " > $@_
	@git show HEAD | sed 's/commit \(.*\)/\"\1/;q' | tr -d \\n >> $@_
	@git diff --quiet || (echo -n ' AND'; git diff --shortstat) | tr -d \\n >> $@_
	@echo '";' >> $@_
	@diff -q $@ $@_ 2>/dev/null >/dev/null || ($(ECHO) "	GEN	" $(notdir $@); cp $@_ $@)
FORCE:
endif


ifeq ("$(origin V)", "command line")
  VERBOSE = $(V)
endif
ifeq (,$(VERBOSE))
  $(eval override CC = @$(ECHO)    "	CC	" $$@; $(CC))
  $(eval override RAGEL = @$(ECHO) "	RGL	" $$@; $(RAGEL))
  $(eval override DOT = @$(ECHO) "	DOT	" $$@; $(DOT))
  $(eval override CONFETTI = @$(ECHO) "	CNF	" $$@; $(CONFETTI))
  $(eval override CAT = @$(ECHO) "	CAT	" $$@; $(CAT))
endif

endif
