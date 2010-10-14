
# This makefile based on ideas from http://make.paulandlesley.org/
# Thanks to Paul D. Smith <psmith@gnu.org>

ECHO=/bin/echo
CAT=/bin/cat

# make magic
SUB_MAKE:=$(filter _%,$(notdir $(CURDIR)))
OBJDIR:=$(shell $(ECHO) $(filter _%,$(MAKECMDGOALS)) | tr ' ' '\n' | cut -d/ -f1 | sort | uniq)
ifeq (,$(SUB_MAKE))
  ifneq (,$(OBJDIR))
    .SUFFIXES:
    MAKEFLAGS += -rR --no-print-directory VPATH=$(CURDIR)
    FILTERED_MAKECMDGOALS=$(subst $@/,,$(filter $@/%,$(MAKECMDGOALS)))
    module=$(subst tarantool_,,$(FILTERED_MAKECMDGOALS))
    .PHONY: $(OBJDIR)
    $(OBJDIR):
	+@mkdir -p $@
	+@$(MAKE) -C $@ -f $(CURDIR)/Makefile SRCDIR=$(CURDIR) OBJDIR=$@ module=$(module) $(FILTERED_MAKECMDGOALS)

    Makefile: ;
    %.mk :: ;
    % :: $(OBJDIR) ; @:
  else
    SRCDIR:=$(CURDIR)
    include $(SRCDIR)/scripts/rules.mk
  endif
else
  include $(SRCDIR)/scripts/rules.mk
endif

ifeq ("$(origin module)", "command line")
.PHONY: clean
clean:
	@echo "	CLEAN $(module)"
	@rm -rf $(obj) $(dep) tarantool_$(module) _* lcov test
else
.PHONY: clean
clean:
	@for mod in mod/*; do $(MAKE) --no-print-directory module=`basename $$mod` clean; done
endif
.PHONY: TAGS
TAGS:
	ctags -R -e -f TAGS


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
