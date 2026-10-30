TARGET = a36se-fw

# ── toolchain ─────────────────────────────────────────────────
PREFIX = arm-none-eabi-
CC  = $(PREFIX)gcc
AS  = $(PREFIX)gcc -x assembler-with-cpp
CP  = $(PREFIX)objcopy
SZ  = $(PREFIX)size
BIN = $(CP) -O binary -S

# ── build dir ─────────────────────────────────────────────────
BUILD = build

# ── sources ───────────────────────────────────────────────────
LIB = lib/gd32f3x0
C_SOURCES = \
  src/main.c \
  src/system.c \
  src/debug.c \
  src/progmode.c \
  src/fault.c \
  drivers/lcd.c \
  drivers/keypad.c \
  drivers/bk4819.c \
  drivers/spiflash.c \
  src/board.c \
  src/radio.c \
  src/scan.c \
  src/channels.c \
  src/watch.c \
  src/ui.c \
  src/shell.c \
  src/settings.c \
  src/voice.c \
  src/tx.c \
  src/msg.c \
  src/aprs.c \
  src/pocsag.c \
  src/splash.c \
  drivers/adc.c \
  gfx/font_5x8.c \
  gfx/font_7x13b.c \
  gfx/font_digits21.c \
  gfx/font_digits12.c \
  gfx/gfx.c \
  gfx/font_6x10.c \
  gfx/font_10x20.c \
  $(LIB)/Peripheral/Source/gd32f3x0_rcu.c \
  $(LIB)/Peripheral/Source/gd32f3x0_gpio.c \
  $(LIB)/Peripheral/Source/gd32f3x0_spi.c \
  $(LIB)/Peripheral/Source/gd32f3x0_misc.c

ASM_SOURCES = startup/startup_a36.S

# ── includes ──────────────────────────────────────────────────
C_INCLUDES = \
  -I$(LIB)/CMSIS \
  -I$(LIB)/CMSIS/GD/GD32F3x0/Include \
  -I$(LIB)/Peripheral/Include \
  -Isrc \
  -Idrivers \
  -Igfx

# ── flags ─────────────────────────────────────────────────────
# Cortex-M4 WITHOUT FPU (MVFR0 = 0 on the radio): software floating point only
CPU = -mcpu=cortex-m4 -mthumb -mfloat-abi=soft

C_DEFS = -DGD32F350 -DUSE_STDPERIPH_DRIVER

CFLAGS  = $(CPU) $(C_DEFS) $(C_INCLUDES) -Os -Wall -Wextra -Wno-unused-parameter
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -g -gdwarf-2
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"
CFLAGS += -std=c11

ASFLAGS = $(CPU) $(C_DEFS) -Wall

LDSCRIPT = link.ld
LDFLAGS  = $(CPU) --specs=nano.specs --specs=nosys.specs -T$(LDSCRIPT)
LDFLAGS += -Wl,-Map=$(BUILD)/$(TARGET).map,--cref -Wl,--gc-sections
LIBS     = -lc -lm -lnosys

# The bootloader occupies the first 1 KB of the 64 KB flash: images must fit in 63 KB.
MAX_IMAGE = 64512

# ── objects ───────────────────────────────────────────────────
OBJECTS  = $(addprefix $(BUILD)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))

OBJECTS += $(addprefix $(BUILD)/,$(notdir $(ASM_SOURCES:.S=.o)))
vpath %.S $(sort $(dir $(ASM_SOURCES)))

# ── targets ───────────────────────────────────────────────────
all: $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).kdhx

$(BUILD)/%.o: %.c Makefile | $(BUILD)
	$(CC) -c $(CFLAGS) $< -o $@

$(BUILD)/%.o: %.S Makefile | $(BUILD)
	$(AS) -c $(ASFLAGS) $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CC) $(OBJECTS) $(LDFLAGS) $(LIBS) -o $@
	$(SZ) $@

$(BUILD)/$(TARGET).bin: $(BUILD)/$(TARGET).elf
	$(BIN) $< $@
	@size=$$(wc -c < $@); echo "Image: $$size of $(MAX_IMAGE) bytes ($$(( size * 100 / $(MAX_IMAGE) ))%)"; \
	  if [ $$size -gt $(MAX_IMAGE) ]; then echo "ERROR: image too big for the 63 KB app area"; rm -f $@; exit 1; fi

# ── .kdhx encryption ─────────────────────────────────────────
$(BUILD)/encrypt: tools/encrypt.c | $(BUILD)
	gcc -O2 -o $@ $<

$(BUILD)/$(TARGET).kdhx: $(BUILD)/$(TARGET).bin $(BUILD)/encrypt
	$(BUILD)/encrypt -e $< $@
	@echo "──────────────────────────────────────"
	@echo "Firmware: $@ ($$(wc -c < $@) bytes)"
	@echo "──────────────────────────────────────"

# ── flash via talkpod-manager ─────────────────────────────────
flash: $(BUILD)/$(TARGET).kdhx
	talkpod-firmware flash --yes $<

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean flash

-include $(wildcard $(BUILD)/*.d)
