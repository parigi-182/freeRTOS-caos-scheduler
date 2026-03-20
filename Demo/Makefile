# The directory that contains FreeRTOS source code
FREERTOS_ROOT := ../FreeRTOS

# Demo code
DEMO_PROJECT := .

# FreeRTOS kernel
KERNEL_DIR := $(FREERTOS_ROOT)/Source
KERNEL_PORT_DIR := $(KERNEL_DIR)/portable/GCC/ARM_CM3

#ptlProject
PTL_DIR  := $(KERNEL_DIR)/PtlProject
PTL_SRC_DIR  := $(PTL_DIR)/Source
PTL_INC_DIR  := $(PTL_DIR)/Source/include
# Where to store all the generated files (objects, elf and map)
OUTPUT_DIR := ./Output

DEMO_NAME := demo
TEST_NAME := test
ELF_APP := $(OUTPUT_DIR)/$(DEMO_NAME).elf
ELF_TEST := $(OUTPUT_DIR)/$(TEST_NAME).elf
MAP := $(OUTPUT_DIR)/$(DEMO_NAME).map

# Compiler toolchain
CC := arm-none-eabi-gcc
LD := arm-none-eabi-gcc
SIZE := arm-none-eabi-size

# Target embedded board and CPU
MACHINE := mps2-an385 
CPU := cortex-m3


# Use -s to connect to gdb port 1234 and -S to wait before executing
QEMU_FLAGS_DBG = -s -S 

INCLUDE_DIRS = -I$(KERNEL_DIR)/include -I$(KERNEL_PORT_DIR)
INCLUDE_DIRS += -I$(PTL_INC_DIR)
INCLUDE_DIRS += -I$(DEMO_PROJECT)
INCLUDE_DIRS += -I$(DEMO_PROJECT)/test/unity

VPATH += $(KERNEL_DIR) $(KERNEL_PORT_DIR) $(KERNEL_DIR)/portable/MemMang
VPATH += $(DEMO_PROJECT)
VPATH += $(DEMO_PROJECT)/test
VPATH += $(DEMO_PROJECT)/test/unity
VPATH += $(PTL_SRC_DIR)                    
VPATH += $(APP_DIR)                    

# Include paths. See INCLUDE_DIRS
CFLAGS = $(INCLUDE_DIRS)

# Don't include standard libraries
CFLAGS += -ffreestanding

# Target ARM cortex M3 processor
CFLAGS += -mcpu=$(CPU)
# Use the thumb 16 bits instruction set. Required for cortex-M
CFLAGS += -mthumb

# Print all the most common warnings
CFLAGS += -Wall

# Print extra warnings, not included in -Wall
CFLAGS += -Wextra

# Print warnings about variable shadowing (var name = outer scope var name)
CFLAGS += -Wshadow

# Include debug information (-g) with maximum level of detail (3)
CFLAGS += -g3

# Optimize (-O) the size (s) of the generated executable 
CFLAGS += -Os

#random debuggin
CFLAGS += -O0

ifdef DEFINEWRITTEN
    CFLAGS += -DDEFINEWRITTEN=$(DEFINEWRITTEN)
endif

# Place each function in a dedicated section (see -gc-sections in LDFLAGS)
CFLAGS += -ffunction-sections

# Place each variable in a dedicated section (see -gc-sections in LDFLAGS)
CFLAGS += -fdata-sections

#start from test 1 
TEST_ID ?= 1
#inject the new value in main to execute a different test each time
CFLAGS += -DconfigUse=$(TEST_ID)

# ... (resto del Makefile)
# Specify the linker script
LDFLAGS = -T ./mps2_m3.ld

# Don't use the standard start-up files
LDFLAGS += -nostartfiles

# Optimize compilation to reduce memory
LDFLAGS += -specs=nano.specs

# Optimize compilation targeting a system without any operating system
LDFLAGS += -specs=nosys.specs

# Generate map file with memory layout. Pass option to the linker (-Xlinker)
LDFLAGS += -Xlinker -Map=$(MAP)

# Remove unused sections. Used with -ffunction-sections and -fdata-sections
# optimized memory. Pass option to the linker (-Xlinker)
LDFLAGS += -Xlinker --gc-sections

# Link specifically for cortex M3 and thumb instruction set
LDFLAGS += -mcpu=$(CPU) -mthumb

# Kernel files
SOURCE_FILES += $(KERNEL_DIR)/list.c
SOURCE_FILES += $(KERNEL_DIR)/tasks.c
SOURCE_FILES += $(KERNEL_DIR)/timers.c
SOURCE_FILES += $(KERNEL_DIR)/queue.c
SOURCE_FILES += $(PTL_SRC_DIR)/ptl.c              
SOURCE_FILES += $(PTL_SRC_DIR)/polling_server.c   
#SOURCE_FILES += $(PTL_SRC_DIR)/trace.c            
SOURCE_FILES += $(PTL_SRC_DIR)/ptl_log.c          
#SOURCE_FILES += $(PTL_SRC_DIR)/ptl_stats.c        
SOURCE_FILES += $(PTL_SRC_DIR)/uart.c
SOURCE_FILES += $(KERNEL_DIR)/portable/MemMang/heap_4.c
SOURCE_FILES += $(KERNEL_DIR)/portable/GCC/ARM_CM3/port.c

# Demo files
# SOURCE_FILES += $(DEMO_PROJECT)/main.c


$(OUTPUT_DIR)/test.o: CFLAGS += -I$(KERNEL_DIR)
$(OUTPUT_DIR)/unity.o: CFLAGS += -I$(KERNEL_DIR)

# Start-up code
SOURCE_FILES += ./startup.c

# 2. Create Common Objects List
# Step A: Get just the filenames (remove paths): "tasks.c uart.c ptl.c"
COMMON_NAMES = $(notdir $(SOURCE_FILES:%.c=%.o))

# Step B: Add the Output folder: "Output/tasks.o Output/uart.o Output/ptl.o"
COMMON_OBJS = $(COMMON_NAMES:%.o=$(OUTPUT_DIR)/%.o)

# 3. Define the Final Lists
# Main app = Common files + main.o
APP_OBJS  = $(COMMON_OBJS) $(OUTPUT_DIR)/main.o

# Test app = Common files + test.o
TEST_OBJS = $(COMMON_OBJS) $(OUTPUT_DIR)/test.o $(OUTPUT_DIR)/unity.o
# --- Targets ---

.PHONY: all test clean qemu_start qemu_debug gdb_start

all: $(ELF_APP)

# Link the Standard App (Use APP_OBJS)
$(ELF_APP): $(APP_OBJS) ./mps2_m3.ld Makefile
	@echo "\n--- Linking Main App ---\n"
	$(LD) $(LDFLAGS) $(APP_OBJS) -o $(ELF_APP)
	$(SIZE) $(ELF_APP)

# Link the Test App (Use TEST_OBJS)
# Run this by typing: make test
Output/unity.o: CFLAGS += -DUNITY_INCLUDE_CONFIG_H
Output/test.o:  CFLAGS += -DUNITY_INCLUDE_CONFIG_H
Output/unity.o: CFLAGS += -fno-unwind-tables
Output/test.o:  CFLAGS += -fno-unwind-tables

test: $(ELF_TEST)

$(ELF_TEST): $(TEST_OBJS) ./mps2_m3.ld Makefile
	@echo "\n--- Linking Test App ---\n"
	$(LD) $(LDFLAGS) $(TEST_OBJS) -o $@
	$(SIZE) $@

# Compiler Rule (Pattern Matching)
# This works for main.c, test.c, and all source files because of VPATH
$(OUTPUT_DIR)/%.o : %.c Makefile | $(OUTPUT_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

cleanobj:
	rm -f $(OUTPUT_DIR)/*.o

clean:
	rm -rf $(ELF_APP) $(ELF_TEST) $(MAP) $(OUTPUT_DIR)/*.o $(OUTPUT_DIR)

qemu_start:
	qemu-system-arm -machine $(MACHINE) -cpu $(CPU) -kernel \
	$(ELF_APP) -monitor none -nographic -serial stdio

qemu_start_test:
	qemu-system-arm -machine $(MACHINE) -cpu $(CPU) -kernel \
	$(ELF_TEST) -monitor none -nographic -serial stdio

qemu_debug:
#added another make for developing and debugging confort
	make
	qemu-system-arm -machine $(MACHINE) -cpu $(CPU) -kernel \
	$(ELF_APP) -monitor none -nographic -serial stdio $(QEMU_FLAGS_DBG)
gdb_start:
	gdb-multiarch $(ELF_APP)
	
gdb_arm:
	arm-none-eabi-gdb $(ELF_APP)