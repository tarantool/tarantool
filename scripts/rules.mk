ifeq (,$(module))
all:
	@echo "Valid targets are:"
	@echo "	_release_box/tarantool_silverbox"
	@echo "	_release_feeder/tarantool_feeder"
	@echo "	_debug_box/tarantool_silverbox"
	@echo "	_debug_feeder/tarantool_feeder"
	@echo "	clean"
else
all: tarantool_$(module)
endif


-include $(SRCDIR)/config.mk $(SRCDIR)/scripts/config.mk
include $(SRCDIR)/scripts/config_def.mk

ifneq (,$(findstring _release,$(OBJDIR)))
  CFLAGS += -DNDEBUG
else ifneq (,$(findstring _debug,$(OBJDIR)))
  DEBUG=1
  CFLAGS += -DDEBUG -fno-omit-frame-pointer
else ifneq (,$(findstring _test,$(OBJDIR)))
 CFLAGS += --coverage -DCOVERAGE -DNDEBUG
else ifneq (,$(findstring _coverage,$(OBJDIR)))
 CFLAGS += --coverage -DCOVERAGE
endif

# build dir includes going first
ifneq (,$(OBJDIR))
CFLAGS += -I$(SRCDIR)/$(OBJDIR) -I$(SRCDIR)/$(OBJDIR)/include
endif
CFLAGS += -I$(SRCDIR) -I$(SRCDIR)/include
LIBS += -lm

subdirs += third_party
ifneq (,$(module))
  subdirs += mod/$(module)
endif
subdirs += cfg core
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
