.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "DEVKITARM n'est pas defini. Installe devkitPro/devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET      := MaGalerie-3DS
BUILD       := build
SOURCES     := source
INCLUDES    := include
DATA        := data

APP_TITLE       := MaGalerie 3DS
APP_DESCRIPTION := Galerie photo personnelle
APP_AUTHOR      := gagnonthe
ICON            := icon.png
RSF_FILE        := app/build-cia.rsf

MAKEROM    ?= makerom
BANNERTOOL ?= bannertool

ARCH        := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft
CFLAGS      := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH)
CFLAGS      += $(INCLUDE) -D__3DS__
CXXFLAGS    := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17
ASFLAGS     := -g $(ARCH)
LDFLAGS     := -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)
LIBS        := -lctru -lm
LIBDIRS     := $(CTRULIB)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT := $(CURDIR)/$(TARGET)
export TOPDIR := $(CURDIR)

export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD := $(CXX)

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES := $(OFILES_BIN) $(OFILES_SRC)

export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON := $(CURDIR)/$(ICON)
export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS += --smdh=$(OUTPUT).smdh

.PHONY: all cia clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

cia: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile cia

$(BUILD):
	@mkdir -p $@

clean:
	@echo Nettoyage...
	@rm -fr $(BUILD)
	@rm -f $(TARGET).3dsx
	@rm -f $(TARGET).elf
	@rm -f $(TARGET).smdh
	@rm -f $(TARGET).cia

else

DEPENDS := $(OFILES:.o=.d)

.PHONY: all cia

all: $(OUTPUT).3dsx

$(OUTPUT).elf: $(OFILES)

$(OUTPUT).smdh:
	@$(BANNERTOOL) makesmdh \
		-i "$(TOPDIR)/$(ICON)" \
		-s "$(APP_TITLE)" \
		-l "$(APP_DESCRIPTION)" \
		-p "$(APP_AUTHOR)" \
		-o "$(OUTPUT).smdh"

$(OUTPUT).3dsx: $(OUTPUT).elf $(OUTPUT).smdh

cia: $(OUTPUT).cia

$(OUTPUT).cia: $(OUTPUT).elf $(OUTPUT).smdh
	@echo Creation du CIA...
	@$(MAKEROM) \
		-exefslogo \
		-f cia \
		-target t \
		-o "$(OUTPUT).cia" \
		-elf "$(OUTPUT).elf" \
		-rsf "$(TOPDIR)/$(RSF_FILE)" \
		-icon "$(OUTPUT).smdh"

-include $(DEPENDS)

endif