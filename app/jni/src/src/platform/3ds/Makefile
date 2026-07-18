#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=$(DEVKITPRO)/devkitARM")
endif

include $(DEVKITARM)/3ds_rules

#---------------------------------------------------------------------------------
# PROJECT CONFIG
#---------------------------------------------------------------------------------
# No ROMFS: zelda3_assets.dat is deliberately NOT bundled into the .3dsx —
# it is read from sd:/3ds/zelda3/ at runtime (see main3ds.c).
TARGET      := zelda3
BUILD       := build
ROMFS       :=

# SRC_ROOT = app/jni/src/ (three levels up from src/platform/3ds/)
SRC_ROOT    := ../../..

APP_TITLE   := The Legend of Zelda: A Link to the Past (Dual Screen)
APP_AUTHOR  := snesrev, Lywx, JeodC, apadin
APP_VERSION := $(shell git -C $(SRC_ROOT) rev-parse --short HEAD 2>/dev/null || echo "dev")

#---------------------------------------------------------------------------------
# SOURCE DIRS (relative paths — devkitARM prepends $(CURDIR)/ to each in VPATH)
#---------------------------------------------------------------------------------
GAME_SRC    := $(SRC_ROOT)/src
SNES_SRC    := $(SRC_ROOT)/snes
PLAT_SRC    := .
OPUS_SRC    := $(SRC_ROOT)/third_party/opus-1.3.1-stripped

SOURCES     := $(GAME_SRC) $(SNES_SRC) $(PLAT_SRC) $(OPUS_SRC)

# Only relative paths here — the Makefile template prepends $(CURDIR)/ to each.
# Absolute paths (DEVKITPRO, portlibs) go directly into CFLAGS below.
INCLUDES    := include $(SRC_ROOT) $(SRC_ROOT)/src

#---------------------------------------------------------------------------------
# COMPILER FLAGS
#---------------------------------------------------------------------------------
ARCH        := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS      := -g -Wall -O2 -mword-relocations \
               -ffunction-sections \
               $(ARCH)

# Absolute includes must be added here, not in INCLUDES (which gets CURDIR prepended).
CFLAGS      += $(INCLUDE) -D__3DS__ \
               -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 \
               -Wno-parentheses -Wno-unused-function -Wno-format \
               -I$(DEVKITPRO)/portlibs/3ds/include/SDL \
               `$(PREFIX)pkg-config opusfile --cflags 2>/dev/null || echo ""`

CXXFLAGS    := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11

ASFLAGS     := -g $(ARCH)
LDFLAGS     =  -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS        := -lctru -lm -lSDL \
               `$(PREFIX)pkg-config opusfile --libs 2>/dev/null || echo "-lopusfile -lopus -logg"` \
               -L$(DEVKITPRO)/portlibs/3ds/lib/

LIBDIRS     := $(PORTLIBS) $(CTRULIB)

#---------------------------------------------------------------------------------
# don't edit below unless adding new file types
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)

export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))

export DEPSDIR  := $(CURDIR)/$(BUILD)

# Game source: all .c in src/ EXCEPT platform-specific desktop files
_GAME_C := $(wildcard $(GAME_SRC)/*.c)
_GAME_C := $(filter-out %/main.c %/opengl.c %/glsl_shader.c, $(_GAME_C))

# SNES emulation source
_SNES_C := $(wildcard $(SNES_SRC)/*.c)

# 3DS platform source (this directory — PLAT_SRC=. is relative to the Makefile)
_PLAT_C := $(wildcard $(CURDIR)/*.c)

# Opus decoder (amalgam file)
_OPUS_C := $(OPUS_SRC)/opus_decoder_amalgam.c

export CFILES   := $(notdir $(_GAME_C)) $(notdir $(_SNES_C)) $(notdir $(_PLAT_C)) $(notdir $(_OPUS_C))
export CPPFILES :=
export SFILES   :=

export OFILES_SOURCES := $(CFILES:.c=.o)
export OFILES := $(OFILES_SOURCES)

# Use CC (arm-none-eabi-gcc) for linking — LD is not set by default in base_tools
ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXDEPS := $(OUTPUT).smdh

export _3DSXFLAGS += --smdh=$(OUTPUT).smdh

ifneq ($(ROMFS),)
export _3DSXFLAGS += --romfs=$(CURDIR)/$(ROMFS)
endif

.PHONY: all clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@echo Cleaning...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx : $(OUTPUT).elf $(_3DSXDEPS)

$(OUTPUT).elf : $(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------------
