CORE_PATH = Xilinx_Cores

vpath %.xco $(CORE_PATH)
vpath %.vhd $(CORE_PATH)

XPSDIR=Synthesis/Supervisor_Micro
SDKDIR=$(XPSDIR)/SDK
WORKSPACEDIR=$(SDKDIR)/SDK_Workspace_35


all: mbbl mbbl_copy mbbl_copy-bit

.PHONY: clean cores synthesis device-tree mbbl mbbl_copy

mbbl: hwexport
# Make sure the system XML description is up to date
	mkdir -p $(WORKSPACEDIR)/hw_platform_0/
	cp $(SDKDIR)/SDK_Export/hw/Supervisor_Micro.xml $(WORKSPACEDIR)/hw_platform_0/system.xml
# Make the standalone BSP
	cd $(WORKSPACEDIR)/standalone_bsp_0 && make all
# Make the mbbl elf
	-rm $(SDKDIR)/SDK_Projects/mbbl/cal-ics/mbbl.elf
	cd $(SDKDIR)/SDK_Projects/mbbl/cal-ics && make all

mbbl_copy: hwexport
# Make sure the system XML description is up to date
	mkdir -p $(WORKSPACEDIR)/hw_platform_0/
	cp $(SDKDIR)/SDK_Export/hw/Supervisor_Micro.xml $(WORKSPACEDIR)/hw_platform_0/system.xml
# Make the standalone BSP
	cd $(WORKSPACEDIR)/standalone_bsp_0 && make all
# Make the mbbl_copy elf
	-rm $(SDKDIR)/SDK_Projects/mbbl_copy/cal-ics/mbbl_copy.elf
	echo "#define MAINTENANCE_MODE" >  $(SDKDIR)/SDK_Projects/mbbl_copy/boot-config.h
	cd $(SDKDIR)/SDK_Projects/mbbl_copy/cal-ics && make clean all
	mv $(SDKDIR)/SDK_Projects/mbbl_copy/cal-ics/mbbl_copy.elf $(SDKDIR)/SDK_Projects/mbbl_copy/cal-ics/mbbl_copy-maint.elf
	echo "#define REGULAR_MODE" >  $(SDKDIR)/SDK_Projects/mbbl_copy/boot-config.h
	cd $(SDKDIR)/SDK_Projects/mbbl_copy/cal-ics && make clean all
	mv $(SDKDIR)/SDK_Projects/mbbl_copy/cal-ics/mbbl_copy.elf $(SDKDIR)/SDK_Projects/mbbl_copy/cal-ics/mbbl_copy-regular.elf

mbbl_copy-bit:
	data2mem -bm Synthesis/edkBmmFile_bd.bmm -bd $(SDKDIR)/SDK_Projects/mbbl_copy/cal-ics/mbbl_copy-maint.elf -bt Synthesis/Supervisor_Micro/implementation/Supervisor_Micro.bit -o b Synthesis/CAL-ICS-FPGA-MBBL-MAINT.bit
	data2mem -bm Synthesis/edkBmmFile_bd.bmm -bd $(SDKDIR)/SDK_Projects/mbbl_copy/cal-ics/mbbl_copy-regular.elf -bt Synthesis/Supervisor_Micro/implementation/Supervisor_Micro.bit -o b Synthesis/CAL-ICS-FPGA-MBBL-REGULAR.bit

#hwexport:
## Ignore the export error when running xdsgen (due to too many busses apparently)
#	-cd $(XPSDIR) && xps -nw -scr ./Export.tcl Supervisor_Micro.xmp
## Run the export step manually which will have the correct path to skip xdsgen
#	cd $(XPSDIR) && make -f Supervisor_Micro.make exporttosdk

#Dummy entry for build outside the tree
hwexport:
	/bin/true

device-tree:
# Generate the device tree
	cd $(WORKSPACEDIR)/device-tree_bsp_0 && make all

synthesis:
	cd $(XPSDIR) && xps -nw -scr ./Build.tcl Supervisor_Micro.xmp
	cd Synthesis && xtclsh ./Build.tcl

cores: $(CORES:.xco=.vhd)

%.vhd: %.xco
	mkdir -p tmp/$(CORE_PATH)
	cp $< tmp/$(CORE_PATH)
	cd tmp/$(CORE_PATH) && coregen -b ../$<
	cp tmp/$(subst xco,ngc,$<) $(CORE_PATH)/
	cp tmp/$(subst xco,vhd,$<) $(CORE_PATH)/

sdk_clean:
	-cd $(WORKSPACEDIR)/standalone_bsp_0 && make clean
	-cd $(WORKSPACEDIR)/device-tree_bsp_0 && make clean
	-cd $(SDKDIR)/SDK_Projects/mbbl/cal-ics && make clean
	-cd $(SDKDIR)/SDK_Projects/mbbl_copy/cal-ics && make clean ; rm -f mbbl_copy-*.elf

clean: sdk_clean
	cd Synthesis && xtclsh ./Clean.tcl
	cd $(XPSDIR) && xps -nw -scr ./Clean.tcl Supervisor_Micro.xmp
	rm -rf tmp

