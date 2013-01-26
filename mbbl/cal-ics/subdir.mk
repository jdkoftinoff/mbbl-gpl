################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../dmitri_io_button.c \
../mbbl.c \
../mvSwitch_6350R.c \
../labx_ethernet.c \
../avb-boot.c \
../fsl_icap.c \
../mbbl-console.c 

LD_SRCS += \
../lscript.ld 

OBJS += \
./dmitri_io_button.o \
./mbbl.o \
./mvSwitch_6350R.o \
./labx_ethernet.o \
./avb-boot.o \
./fsl_icap.o \
./mbbl-console.o 

C_DEPS += \
./dmitri_io_button.d \
./mbbl.d \
./mvSwitch_6350R.d \
./labx_ethernet.d \
./avb-boot.d \
./fsl_icap.d \
./mbbl-console.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo Building file: $<
	@echo Invoking: MicroBlaze gcc compiler
	mb-gcc -Wall -O2 -c -fmessage-length=0 -I../../../SDK_Workspace_35/standalone_bsp_0/microblaze_0/include -I../zlib-1.2.3 -mxl-barrel-shift -mxl-pattern-compare -mno-xl-soft-div -mcpu=v8.10.a -mno-xl-soft-mul -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo Finished building: $<
	@echo ' '


