# RH850/F1KM-S1 Bare-Metal Demo - Top-level Makefile
#
# Usage:
#   make BOARD=983HH APP=blink_led
#   make BOARD=983HH APP=i2c_slave
#   make BOARD=983HH APP=i2c_slave DEBUG=on
#   make BOARD=983HH APP=i2c_slave VERSION=01.10
#   make clean
#   make info
#
# Available APPs: blink_led, mirror_dip, i2c_slave, i2c_master_pcf8574, i2c_bitbang

BOARD ?= 983HH
APP   ?= blink_led

# ---------------------------------------------------------------------------
# CC-RH Toolchain
# ---------------------------------------------------------------------------
CCRH_DIR := /usr/local/Renesas/CC-RH/V2.07.00
CC       := $(CCRH_DIR)/bin/ccrh
LD       := $(CCRH_DIR)/bin/rlink
LIB_DIR  := $(CCRH_DIR)/lib/v850e3v5

# Project root (absolute paths for CC-RH include resolution from BUILD_DIR)
PROJ_ROOT := $(abspath .)

# Build variant suffix (debug vs release)
ifeq ($(DEBUG),on)
    VARIANT := debug
else
    VARIANT := release
endif

# WSL workaround: CC-RH (32-bit) cannot access /mnt/c paths.
# All compilation happens in /tmp.
BUILD_DIR := /tmp/rh850_$(BOARD)_$(APP)_$(VARIANT)_build
OUT_DIR   := output/$(BOARD)/$(APP)/$(VARIANT)

# ---------------------------------------------------------------------------
# R7F701686 Memory Map
# ---------------------------------------------------------------------------
#   Code Flash: 0x00000000, 512 KB
#   Local RAM:  0xFEBF0000, 32 KB
RESET_ADDR := 00000000
TEXT_ADDR  := 00000A00
DATA_ADDR  := FEBF0000

# ---------------------------------------------------------------------------
# Sources
# ---------------------------------------------------------------------------
APP_SRC   := $(wildcard app/$(APP)/*.c)
BOARD_SRC := board/$(BOARD)/board_init.c
HAL_SRC   := $(wildcard hal/*.c)
LIB_SRC   := $(wildcard lib/*.c)
ASM_SRC   := startup/boot.asm startup/cstart.asm

ALL_C_SRC := $(APP_SRC) $(BOARD_SRC) $(HAL_SRC) $(LIB_SRC)

# Include paths (absolute, resolved from BUILD_DIR)
INCLUDES  := -I$(CCRH_DIR)/inc \
             -I$(PROJ_ROOT)/device \
             -I$(PROJ_ROOT)/app/$(APP) \
             -I$(PROJ_ROOT)/board/$(BOARD) \
             -I$(PROJ_ROOT)/hal \
             -I$(PROJ_ROOT)/lib

# Compiler flags
CFLAGS    := -Xcommon=rh850 -g -c $(INCLUDES)
ASMFLAGS  := -Xcommon=rh850 -g -c

# Firmware version (BCD format: make VERSION=01.10 -> v1.10)
VERSION     ?= 00.00
VERSION_MAJ := $(word 1,$(subst ., ,$(VERSION)))
VERSION_MIN := $(word 2,$(subst ., ,$(VERSION)))
CFLAGS      += -DFW_VERSION_MAJOR=0x$(VERSION_MAJ)u -DFW_VERSION_MINOR=0x$(VERSION_MIN)u

ifeq ($(DEBUG),on)
    CFLAGS += -DDEBUG_ENABLED
endif

# Object files (flattened into BUILD_DIR)
C_OBJS    := $(addprefix $(BUILD_DIR)/,$(notdir $(ALL_C_SRC:.c=.obj)))
ASM_OBJS  := $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SRC:.asm=.obj)))
ALL_OBJS  := $(ASM_OBJS) $(C_OBJS)

TARGET    := $(BOARD)_$(APP)

# VPATH for source file discovery
vpath %.c   app/$(APP) board/$(BOARD) hal lib
vpath %.asm startup

# ---------------------------------------------------------------------------
# Linker section layout
# ---------------------------------------------------------------------------
# RESET          @ 0x00000000  (exception vector table, 512 bytes)
# EIINTTBL       @ 0x00000200  (interrupt vector table, 2 KB)
# .text,.const   @ 0x00000A00  (code + read-only data)
# .INIT_DSEC/BSEC                (section init tables, follows .const)
# .data,.stack.bss,.bss @ 0xFEBF0000  (RAM)
LINK_SECTIONS := RESET/$(RESET_ADDR),EIINTTBL/00000200,.text,.const,.INIT_DSEC.const,.INIT_BSEC.const/$(TEXT_ADDR),.data,.stack.bss,.bss/$(DATA_ADDR)

# ---------------------------------------------------------------------------
# Build rules
# ---------------------------------------------------------------------------
.PHONY: all clean clean-all info

all: $(OUT_DIR)/$(TARGET).bin

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(OUT_DIR):
	mkdir -p $(OUT_DIR)

# Compile C sources (copy to BUILD_DIR first for WSL workaround)
$(BUILD_DIR)/%.obj: %.c | $(BUILD_DIR)
	cp $< $(BUILD_DIR)/
	cd $(BUILD_DIR) && $(CC) $(CFLAGS) $(notdir $<)

# Assemble ASM sources
$(BUILD_DIR)/%.obj: %.asm | $(BUILD_DIR)
	cp $< $(BUILD_DIR)/
	cd $(BUILD_DIR) && $(CC) $(ASMFLAGS) $(notdir $<)

# Link all objects
$(BUILD_DIR)/$(TARGET).abs: $(ALL_OBJS) | $(OUT_DIR)
	cd $(BUILD_DIR) && $(LD) \
		boot.obj cstart.obj $(filter-out boot.obj cstart.obj,$(notdir $(ALL_OBJS))) \
		-library=$(LIB_DIR)/libc.lib \
		-nologo \
		-start=$(LINK_SECTIONS) \
		-entry=__start \
		-debug \
		-total_size \
		-list=$(TARGET).map \
		-show=symbol \
		-form=absolute \
		-output=$(TARGET).abs \
		-define=__gp_data=$(DATA_ADDR) \
		-define=__ep_data=$(DATA_ADDR) \
		-define=__s.data.R=$(DATA_ADDR)

# Generate Intel HEX
$(OUT_DIR)/$(TARGET).hex: $(BUILD_DIR)/$(TARGET).abs
	cd $(BUILD_DIR) && $(LD) \
		$(TARGET).abs \
		-nologo \
		-form=hexadecimal \
		-output=$(TARGET).hex
	cp $(BUILD_DIR)/$(TARGET).abs $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).map $(OUT_DIR)/

# Generate flash-ready BIN (crop to 512 KB code flash)
$(OUT_DIR)/$(TARGET).bin: $(OUT_DIR)/$(TARGET).hex
	srec_cat $(OUT_DIR)/$(TARGET).hex -Intel -crop 0x0 0x80000 -o $(OUT_DIR)/$(TARGET).bin -Binary

# Show build configuration
info:
	@echo "BOARD      = $(BOARD)"
	@echo "APP        = $(APP)"
	@echo "VERSION    = $(VERSION) (major=0x$(VERSION_MAJ) minor=0x$(VERSION_MIN))"
	@echo "DEBUG      = $(DEBUG)"
	@echo "TARGET     = $(TARGET)"
	@echo "C sources  = $(ALL_C_SRC)"
	@echo "ASM sources= $(ASM_SRC)"
	@echo "Objects    = $(notdir $(ALL_OBJS))"
	@echo "BUILD_DIR  = $(BUILD_DIR)"
	@echo "OUT_DIR    = $(OUT_DIR)"

# ---------------------------------------------------------------------------
# MISRA C static analysis (cppcheck + misra.py addon)
# ---------------------------------------------------------------------------
MISRA_ADDON := misra
MISRA_SRC   := $(wildcard hal/*.c) $(wildcard lib/*.c) \
               $(wildcard app/*/*.c) $(wildcard board/$(BOARD)/*.c)
MISRA_INC   := -I$(PROJ_ROOT)/device -I$(PROJ_ROOT)/board/$(BOARD) \
               -I$(PROJ_ROOT)/hal -I$(PROJ_ROOT)/lib

.PHONY: misra misra-count

# Full MISRA report (all violations with file:line details)
misra:
	@echo "=== MISRA C analysis (cppcheck + misra.py, C:2012 rules) ==="
	@echo "Scope: hal/ lib/ app/ board/$(BOARD)/"
	@echo "Excluded: device/ (vendor header)"
	@echo ""
	cppcheck --addon=$(MISRA_ADDON) --enable=all \
		--suppress=missingIncludeSystem \
		--suppress=unmatchedSuppression \
		--suppress=unusedFunction \
		--suppress="*:$(PROJ_ROOT)/device/*" \
		$(MISRA_INC) \
		--platform=unspecified \
		$(MISRA_SRC) 2>&1 | \
		grep "misra-c2012" | \
		sort -t: -k1,1 -k2,2n
	@echo ""
	@echo "Total violations:"
	@cppcheck --addon=$(MISRA_ADDON) --enable=all \
		--suppress=missingIncludeSystem \
		--suppress=unmatchedSuppression \
		--suppress=unusedFunction \
		--suppress="*:$(PROJ_ROOT)/device/*" \
		$(MISRA_INC) \
		--platform=unspecified \
		$(MISRA_SRC) 2>&1 | \
		grep -c "misra-c2012" || true

# Quick summary: violation count by rule
misra-count:
	@cppcheck --addon=$(MISRA_ADDON) --enable=all \
		--suppress=missingIncludeSystem \
		--suppress=unmatchedSuppression \
		--suppress=unusedFunction \
		--suppress="*:$(PROJ_ROOT)/device/*" \
		$(MISRA_INC) \
		--platform=unspecified \
		$(MISRA_SRC) 2>&1 | \
		grep "misra-c2012" | \
		sed 's/.*\[misra-c2012-//' | sed 's/\]//' | \
		sort | uniq -c | sort -rn

# Show linked section sizes and total (from map file)
size:
	@if [ -f "$(OUT_DIR)/$(TARGET).map" ]; then \
		echo "=== $(TARGET) ($(VARIANT)) ==="; \
		grep "^SECTION " $(OUT_DIR)/$(TARGET).map -A20 | head -20; \
		echo ""; \
		awk '/^SECTION /{found=1;next} found && /^$$/{exit} found && /^ /{total+=strtonum("0x"$$3)} END{printf "Total:  %d bytes (%.1f KB)\nLimit:  262144 bytes (256.0 KB)\nUsage:  %.1f%%\n", total, total/1024, total/262144*100}' $(OUT_DIR)/$(TARGET).map; \
	else \
		echo "No map file found. Build first: make BOARD=$(BOARD) APP=$(APP)"; \
	fi

clean:
	rm -rf $(BUILD_DIR) $(OUT_DIR)

# Clean all apps and build artifacts (all boards)
clean-all:
	rm -rf /tmp/rh850_* output/
