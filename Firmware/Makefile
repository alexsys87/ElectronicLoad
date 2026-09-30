# Battery emulator firmware for the WeAct BlackPill STM32F401.
#
#   make              build build/battery_emu.elf / .bin / .hex
#   make flash        flash with an ST-Link (st-flash)
#   make dfu          flash over USB with the ROM bootloader (dfu-util)
#   make sim          PC build of the model, see sim/sim_main.c
#   make curve        print one simulated discharge as CSV
#   make clean
#
# Options, e.g.:  make CDEFS="-DEMU_TIME_SCALE=120 -DBAT_CAPACITY_MAH=3400"

TARGET    = battery_emu
BUILD     = build

PREFIX   ?= arm-none-eabi-
CC        = $(PREFIX)gcc
OBJCOPY   = $(PREFIX)objcopy
SIZE      = $(PREFIX)size
HOSTCC   ?= cc

SRCS = \
  src/startup.c \
  src/system.c \
  src/board.c \
  src/usb_cdc.c \
  src/px100.c \
  src/battery_emu.c \
  src/main.c

INCLUDES = \
  -Isrc \
  -IDrivers/CMSIS/Include \
  -IDrivers/CMSIS/Device/ST/STM32F4xx/Include

CPU      = -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
WARN     = -Wall -Wextra -Wshadow -Wstrict-prototypes -Wno-unused-parameter
CDEFS   ?=
DEFS     = -DSTM32F401xC $(CDEFS)

CFLAGS   = $(CPU) -std=gnu11 -Os -g3 $(WARN) $(DEFS) $(INCLUDES) \
           -ffunction-sections -fdata-sections -MMD -MP
LDFLAGS  = $(CPU) -T stm32f401xc.ld -nostartfiles \
           --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map \
           -Wl,--print-memory-usage
LDLIBS   = -lm

OBJS = $(SRCS:%.c=$(BUILD)/%.o)

.PHONY: all flash dfu sim curve clean

all: $(BUILD)/$(TARGET).elf $(BUILD)/$(TARGET).bin $(BUILD)/$(TARGET).hex
	@$(SIZE) $(BUILD)/$(TARGET).elf

$(BUILD)/%.o: %.c Makefile
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJS) stm32f401xc.ld
	$(CC) $(LDFLAGS) $(OBJS) $(LDLIBS) -o $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/%.hex: $(BUILD)/%.elf
	$(OBJCOPY) -O ihex $< $@

# ST-Link V2 on SWDIO / SWCLK / GND / 3V3.
flash: $(BUILD)/$(TARGET).bin
	st-flash --reset write $< 0x08000000

# Hold BOOT0, tap NRST, release BOOT0: the board enumerates as
# "STM32 BOOTLOADER" (0483:df11).
dfu: $(BUILD)/$(TARGET).bin
	dfu-util -a 0 -s 0x08000000:leave -D $<

# ---- PC build of the model --------------------------------------------

SIM_SRCS = sim/sim_main.c src/battery_emu.c src/px100.c

sim: $(BUILD)/sim/$(TARGET)_sim

$(BUILD)/sim/$(TARGET)_sim: $(SIM_SRCS) src/*.h Makefile
	@mkdir -p $(dir $@)
	$(HOSTCC) -std=c11 -O2 -Wall -Wextra -Wshadow $(CDEFS) -Isrc $(SIM_SRCS) -lm -o $@

curve: sim
	$(BUILD)/sim/$(TARGET)_sim --curve

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d)
