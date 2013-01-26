#!/bin/bash

MYPATH=`dirname "$0"`;
cd "${MYPATH}" || exit 1

SRC_DIR="`pwd`"
rm -rf ../../../../build
mkdir ../../../../build
cd ../../../../build || exit 1
IMAGE_BUILD_DIR="`pwd`"

if [ "${CAL_AVB_FPGA_SDK_ARCHIVE}" != "" ] ; then
    rm -rf cal-fpga
    mkdir cal-fpga || exit 1
    cd cal-fpga || exit 1
    tar xzf "${CAL_AVB_FPGA_SDK_ARCHIVE}" || exit 1
else
    if [ -d ../../labx-ip/cal/FPGA ] ; then
	cd ../../labx-ip/cal/FPGA/ || exit 1
    else
	echo "Directory with FPGA SDK workspace is not present in the build"
	echo "and there is no archive of it passed as CAL_AVB_FPGA_SDK_ARCHIVE"
	exit 1
    fi
fi
FPGA_BUILD_DIR="`pwd`"

rm -rf Synthesis/Supervisor_Micro/SDK/SDK_Projects/mbbl 
rm -rf Synthesis/Supervisor_Micro/SDK/SDK_Projects/mbbl_copy
cp -r "${SRC_DIR}/../../../mbbl_copy" "${SRC_DIR}/../../../mbbl" Synthesis/Supervisor_Micro/SDK/SDK_Projects/

make -f Synthesis/Supervisor_Micro/SDK/SDK_Projects/mbbl/boards/cal-ics/mbbl-images.mk || exit 1

cp Synthesis/CAL-ICS-FPGA-MBBL-MAINT.bit \
   Synthesis/CAL-ICS-FPGA-MBBL-REGULAR.bit \
   Synthesis/Supervisor_Micro/SDK/SDK_Projects/mbbl/cal-ics/mbbl.elf \
   "${IMAGE_BUILD_DIR}" || exit 1

cd "${IMAGE_BUILD_DIR}"
rm -rf update
mkdir update || exit 1

if [ -f ../../calamp_with_pvb.hex ] ; then
    cp ../../calamp_with_pvb.hex update/cal_amcu.hex
fi

if [ -f ../../dspapp_with_pvb.hex ] ; then
    cp ../../dspapp_with_pvb.hex update/cal_dmcu.hex
fi

mv mbbl.elf update

cp ../../mb-linux-msli/uClinux-dist/linux-2.6.x/arch/microblaze/boot/linux.bin update || exit 1
gzip -9 update/linux.bin

rm -rf romfs
cp -r ../../mb-linux-msli/uClinux-dist/romfs romfs || exit 1
mkdir -p romfs/usr/bin
mkdir -p romfs/etc/config.orig
cp ../../labx-ip/cal/App_Firmware/avb_audio_ctrl \
    ../../labx-ip/cal/App_Firmware/avbd \
    ../../labx-ip/cal/App_Firmware/p17221d \
    ../../labx-ip/cal/App_Firmware/static_avb_config \
    romfs/usr/bin || exit 1
cp ../../labx-ip/cal/App_Firmware/configs/CAL_ICSPlatform.xml \
   ../../labx-ip/cal/App_Firmware/configs/CAL_ICSPlatformStatic.xml \
   ../../labx-ip/cal/App_Firmware/configs/StaticAVBStreams.xml \
    romfs/etc/config.orig || exit 1

genromfs -f update/romfs.bin -d romfs || exit 1
rm -rf romfs

gzip -9 update/romfs.bin

dtc -f -o update/dt.dtb -O dtb "${SRC_DIR}/cal-ics.dts"
cp ../../mb-linux-msli/local/logo-1.bin.gz \
 ../../mb-linux-msli/local/8x12-font.bin.gz \
 ../../mb-linux-msli/local/16x24-font.bin.gz \
 ../../mb-linux-msli/local/identity.txt update || exit 1

../mbbl-mkbootimage/mbbl-imagetool -s 0 -N \
 -d update/dt.dtb \
 -k update/linux.bin.gz \
 -f update/8x12-font.bin.gz \
 -f update/16x24-font.bin.gz \
 -i update/identity.txt \
 -l update/logo-1.bin.gz \
 -r update/romfs.bin.gz \
 -b CAL-ICS-FPGA-MBBL-MAINT.bit \
 -e update/mbbl.elf \
 -o bootimage-maint.bin || exit 1

../../mb-linux-msli/mcsbin/mcsbin -m -o 0  bootimage-maint.bin bootimage-maint.mcs || exit 1

../mbbl-mkbootimage/mbbl-imagetool -s 0x800000 -N \
 -d update/dt.dtb \
 -k update/linux.bin.gz \
 -f update/8x12-font.bin.gz \
 -f update/16x24-font.bin.gz \
 -i update/identity.txt \
 -l update/logo-1.bin.gz \
 -r update/romfs.bin.gz \
 -b CAL-ICS-FPGA-MBBL-REGULAR.bit \
 -e update/mbbl.elf \
 -o bootimage-regular.bin || exit 1

../../mb-linux-msli/mcsbin/mcsbin -m -o 0  bootimage-regular.bin bootimage-regular.mcs || exit 1

cp CAL-ICS-FPGA-MBBL-REGULAR.bit update/download.bit
tar czf cal-firmware.tar.gz update/

cp CAL-ICS-FPGA-MBBL-MAINT.bit update/download.bit
tar czf cal-firmware-maint.tar.gz update/

rm -rf update

exit 0
