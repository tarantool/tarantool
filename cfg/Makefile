obj += cfg/warning.o
obj += cfg/tarantool_$(module)_cfg.o

$(obj): cfg/tarantool_cfg.h
.PHONY: cfg/tarantool_cfg.h
cfg/tarantool_cfg.h: cfg/tarantool_$(module)_cfg.h
	@mkdir -p cfg
	@ln -sf $(abspath $<) $@

ifeq ($(HAVE_CONFETTI),1)
cfg/tarantool_$(module)_cfg.h: cfg/tarantool_$(module)_cfg.cfg_tmpl
cfg/tarantool_$(module)_cfg.cfg_tmpl: cfg/core_cfg.cfg_tmpl $(cfg_tmpl)
	@mkdir -p $(dir $@)
	@echo '%{ ' > $@
	@echo "#include <third_party/confetti/prscfg.h>" >> $@
	@echo "#include <cfg/tarantool_$(module)_cfg.h>" >> $@
	@echo "void out_warning(ConfettyError r, char *format, ...);" >> $@
	@echo '%}' >> $@
	$(CAT) $^ >> $@
endif

cfg/tarantool_$(module)_cfg.o: CFLAGS += -Wno-unused
