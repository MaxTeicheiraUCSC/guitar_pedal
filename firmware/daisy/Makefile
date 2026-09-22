# libDaisy-style Makefile. Set LIBDAISY_DIR to your libDaisy checkout.
TARGET = pedal
CPP_SOURCES = main.cpp
LIBDAISY_DIR ?= ../../third_party/libDaisy
C_INCLUDES += -I../dsp/include
CPPFLAGS += -fno-exceptions -fno-rtti
# Default APP_TYPE (internal flash, 128 kB). The SDRAM arena lives in SDRAM via
# DSY_SDRAM_BSS regardless. If the image outgrows flash, switch to APP_TYPE=BOOT_SRAM,
# which requires flashing the Daisy bootloader once (make program-boot) and changes
# the update procedure (see README).
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile
