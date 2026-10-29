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
C_SOURCES = \
  src/main.c \
  src/system.c \
  src/gpio.c \
  src/debug.c \
  src/interrupts.c \
  drivers/st7735s.c \
  sdk/CMSIS/GD32F30x/Source/system_gd32f30x.c \
  sdk/Peripheral/Source/gd32f30x_gpio.c \
  sdk/Peripheral/Source/gd32f30x_rcu.c \
  sdk/Peripheral/Source/gd32f30x_spi.c \
  sdk/Peripheral/Source/gd32f30x_misc.c \
  sdk/Peripheral/Source/gd32f30x_dma.c \
  sdk/Peripheral/Source/gd32f30x_usart.c \
  sdk/Peripheral/Source/gd32f30x_timer.c \
  sdk/Peripheral/Source/gd32f30x_adc.c \
  sdk/Peripheral/Source/gd32f30x_fmc.c \
  sdk/Peripheral/Source/gd32f30x_exti.c

ASM_SOURCES = sdk/CMSIS/GD32F30x/Source/GCC/startup_gd32f30x_hd.S

# ── includes ──────────────────────────────────────────────────
C_INCLUDES = \
  -Isdk/CMSIS \
  -Isdk/CMSIS/GD32F30x/Include \
  -Isdk/Peripheral/Include \
  -Isrc \
  -Idrivers

# ── flags ─────────────────────────────────────────────────────
CPU = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

C_DEFS = -DUSE_STDPERIPH_DRIVER -DGD32F30X_HD

CFLAGS  = $(CPU) $(C_DEFS) $(C_INCLUDES) -Os -Wall
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -g -gdwarf-2
CFLAGS += -MMD -MP -MF"$(@:%.o=%.d)"
CFLAGS += -std=c11

ASFLAGS = $(CPU) $(C_DEFS) -Wall -fdata-sections -ffunction-sections

LDSCRIPT = link.ld
LDFLAGS  = $(CPU) -specs=nosys.specs -T$(LDSCRIPT)
LDFLAGS += -Wl,-Map=$(BUILD)/$(TARGET).map,--cref -Wl,--gc-sections
LIBS     = -lc -lm -lnosys

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
	talkpod-firmware flash $<

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD)

.PHONY: all clean flash

-include $(wildcard $(BUILD)/*.d)
