################################################################################
# Automatically-generated file. Do not edit!
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include subdir.mk
-include zlib-1.2.3/subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
ifneq ($(strip $(S_UPPER_DEPS)),)
-include $(S_UPPER_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 
ELFSIZE += \
mbbl.elf.size \

ELFCHECK += \
mbbl.elf.elfcheck \


# All Target
all: mbbl.elf secondary-outputs

# Tool invocations
mbbl.elf: $(OBJS) ../ldscripts/mbbl_linker_script_cal-ics.ld $(USER_OBJS)
	@echo Building target: $@
	@echo Invoking: MicroBlaze gcc linker
	mb-gcc -Wl,-T -Wl,../ldscripts/mbbl_linker_script_cal-ics.ld -L../../../SDK_Workspace_35/standalone_bsp_0/microblaze_0/lib -mxl-barrel-shift -mxl-pattern-compare -mno-xl-soft-div -mcpu=v8.10.a -mno-xl-soft-mul -o"mbbl.elf" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo Finished building target: $@
	@echo ' '

mbbl.elf.size: mbbl.elf
	@echo Invoking: MicroBlaze Print Size
	mb-size mbbl.elf  |tee "mbbl.elf.size"
	@echo Finished building: $@
	@echo ' '

mbbl.elf.elfcheck: mbbl.elf
	@echo Invoking: Xilinx ELF Check
	elfcheck mbbl.elf -hw ../../../SDK_Workspace_35/hw_platform_0/system.xml -pe microblaze_0  |tee "mbbl.elf.elfcheck"
	@echo Finished building: $@
	@echo ' '

# Other Targets
clean:
	-$(RM) $(OBJS)$(C_DEPS)$(EXECUTABLES)$(ELFSIZE)$(ELFCHECK)$(S_UPPER_DEPS) mbbl.elf
	-@echo ' '

secondary-outputs: $(ELFSIZE) $(ELFCHECK)

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets
