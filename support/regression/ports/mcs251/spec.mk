# Regression-test specification for SDCC MCS251 running on uCsim.
#
# The port reuses the standard uCsim/simif machinery (a virtual
# peripheral at xram 0x7654) inherited from the mcs51-common layout,
# so the same support.c / uCsim.cmd pattern works under -t251.  Code is
# linked at the default low 64 KiB (code_loc=0), which matches the
# uCsim mcs251 model; the QEMU/STC32G layout (region FF) is handled by
# run-qemu.py and is not used here.

PORT_BASE = mcs251
MCS251_MODEL ?= small
MCS251_STACK_AUTO ?= 0

EMU_PORT_FLAG = -t251
EMU_FLAGS     = -S in=$(DEV_NULL),out=-
EMU_INPUT     = < $(PORTS_DIR)/$(PORT_BASE)/uCsim.cmd

MCS251_LIBRARY_SUFFIX = $(if $(filter 1,$(MCS251_STACK_AUTO)),-stack-auto,)
MCS251_LIBDIR ?= $(top_builddir)/device/lib/build/mcs251-$(MCS251_MODEL)$(MCS251_LIBRARY_SUFFIX)

# path to uCsim
ifdef SDCC_BIN_PATH
  S51 = $(SDCC_BIN_PATH)/ucsim_51$(EXEEXT)
else
  ifdef UCSIM_DIR
    S51A = $(UCSIM_DIR)/src/sims/s51.src/ucsim_51$(EXEEXT)
  else
    S51A = $(top_builddir)/sim/ucsim/src/sims/s51.src/ucsim_51$(EXEEXT)
    S51B = $(top_builddir)/bin/ucsim_51$(EXEEXT)
  endif

  EMU = $(WINE) $(shell if [ -f $(S51A) ]; then echo $(S51A); else echo $(S51B); fi)

ifndef CROSSCOMPILING
  SDCCFLAGS += --nostdinc -I$(INC_DIR)/mcs51 -I$(top_srcdir)
  LINKFLAGS += --nostdlib -L$(MCS251_LIBDIR)
endif
endif

ifdef CROSSCOMPILING
  DEV_NULL ?= NUL
  SDCCFLAGS += -I$(top_srcdir)
else
  DEV_NULL ?= /dev/null
endif

SDCCFLAGS += -mmcs251 --model-$(MCS251_MODEL) --less-pedantic
ifeq ($(MCS251_STACK_AUTO),1)
  SDCCFLAGS += --stack-auto
endif
# MCS251 has a complete stack-based variadic ABI.  The regression harness
# disables variadic tests by default for historical targets; exercise them
# for this port instead.
ifneq ($(MCS251_NO_VARARGS),1)
  SDCCFLAGS := $(filter-out -DNO_VARARGS,$(SDCCFLAGS))
endif
LINKFLAGS += mcs251.lib libsdcc.lib liblong.lib libint.lib libfloat.lib liblonglong.lib

OBJEXT = .rel
BINEXT = .ihx

# otherwise `make` deletes testfwk.rel and `make -j` will fail
.PRECIOUS: $(PORT_CASES_DIR)/%$(OBJEXT)

EXTRAS = $(PORT_CASES_DIR)/testfwk$(OBJEXT) \
	$(PORT_CASES_DIR)/support$(OBJEXT)
include $(srcdir)/fwk/lib/spec.mk

SPEC_LIB = $(PORTS_DIR)/mcs251/fwk.lib

_clean:
