#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# TARGET     is the name of the output
# BUILD      is the directory where object files & intermediate files will be placed
# SOURCES    is a list of directories containing source code
# DATA       is a list of directories containing data files
# INCLUDES   is a list of directories containing header files
# ROMFS      is the directory containing data to be added to RomFS, relative to the Makefile
#
# NACP building is enabled by default; APP_TITLE / APP_AUTHOR / APP_VERSION
# control the metadata shown on the Switch home menu.
#---------------------------------------------------------------------------------
TARGET      := TicoDLplus
BUILD       := build
SOURCES     := source
DATA        := data
INCLUDES    := include source
ROMFS       := romfs

APP_TITLE   := TicoDL+
APP_AUTHOR  := TicoDL+
# APP_VERSION is set in the top-level branch below: it auto-increments the patch
# component (VERSION file) once per build via bump_version.sh.

#---------------------------------------------------------------------------------
# options for code generation
#---------------------------------------------------------------------------------
ARCH    := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS  := -g -Wall -O2 -ffunction-sections \
           $(ARCH) $(DEFINES)

CFLAGS  += $(INCLUDE) -D__SWITCH__ -DLIBARCHIVE_STATIC

CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17

ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# curl uses the libnx ssl backend (no mbedtls). libarchive pulls bz2/lzma/lz4/
# zstd/expat/z. -lnx must come last.
LIBS    := -lcurl -larchive -lbz2 -llzma -llz4 -lzstd -lexpat -lz -lnx

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level
# containing include and lib
#---------------------------------------------------------------------------------
LIBDIRS := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
# no real need to edit anything past this point unless you need to add additional
# rules for different file extensions
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

# Auto-increment the patch version once per build (skipped for `make clean`).
# Exported so the recursive build-dir make uses it for the NACP.
ifeq ($(filter clean,$(MAKECMDGOALS)),)
export APP_VERSION := $(shell sh $(CURDIR)/bump_version.sh)
else
export APP_VERSION := $(shell cat $(CURDIR)/VERSION 2>/dev/null || echo 0.1.0)
endif

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(CONFIG_JSON)),)
	jsons := $(wildcard *.json)
endif

ifeq ($(strip $(ICON)),)
	icons := $(wildcard *.jpg)
	ifneq (,$(findstring $(TARGET).jpg,$(icons)))
		export APP_ICON := $(TOPDIR)/$(TARGET).jpg
	else
		ifneq (,$(findstring icon.jpg,$(icons)))
			export APP_ICON := $(TOPDIR)/icon.jpg
		endif
	endif
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

ifeq ($(strip $(NO_ICON)),)
	export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
	export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

ifneq ($(APP_TITLEID),)
	export NACPFLAGS += --titleid=$(APP_TITLEID)
endif

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
all : $(OUTPUT).nro

$(OUTPUT).nro : $(OUTPUT).elf $(OUTPUT).nacp
$(OUTPUT).nacp : $(TOPDIR)/VERSION
$(OUTPUT).elf : $(OFILES)

$(OFILES_SRC) : $(HFILES_BIN)

#---------------------------------------------------------------------------------
# you need a rule like this for each extension you use as binary data
#---------------------------------------------------------------------------------
%.bin.o %_bin.h : %.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
