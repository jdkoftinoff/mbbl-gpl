################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../zlib-1.2.3/adler32.c \
../zlib-1.2.3/crc32.c \
../zlib-1.2.3/inffast.c \
../zlib-1.2.3/inflate.c \
../zlib-1.2.3/inftrees.c \
../zlib-1.2.3/zutil.c 

OBJS += \
./zlib-1.2.3/adler32.o \
./zlib-1.2.3/crc32.o \
./zlib-1.2.3/inffast.o \
./zlib-1.2.3/inflate.o \
./zlib-1.2.3/inftrees.o \
./zlib-1.2.3/zutil.o 

C_DEPS += \
./zlib-1.2.3/adler32.d \
./zlib-1.2.3/crc32.d \
./zlib-1.2.3/inffast.d \
./zlib-1.2.3/inflate.d \
./zlib-1.2.3/inftrees.d \
./zlib-1.2.3/zutil.d 


# Each subdirectory must supply rules for building sources it contributes
zlib-1.2.3/%.o: ../zlib-1.2.3/%.c
	@echo Building file: $<
	@echo Invoking: MicroBlaze gcc compiler
	mb-gcc -Wall -O2 -c -fmessage-length=0 -I../../../SDK_Workspace_35/standalone_bsp_0/microblaze_0/include -mxl-barrel-shift -mxl-pattern-compare -mno-xl-soft-div -mcpu=v8.10.a -mno-xl-soft-mul -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o"$@" "$<"
	@echo Finished building: $<
	@echo ' '


