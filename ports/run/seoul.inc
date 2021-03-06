#
# \brief  Using Vancouver/Seoul on Genode
# \author Norman Feske
# \author Markus Partheymueller
# \author Alexander Boettcher
# \date   2011-11-21

#
# Build
#

if {![have_spec nova]} {
	puts "\nSeoul is solely supported on NOVA.\n"
	exit 0
}

set build_components {
	core init
	drivers/timer
	drivers/rtc
	vancouver
	drivers/pci
	drivers/input
}

if {$use_fancy_stuff} { set use_framebuffer 1 }

lappend_if [have_spec acpi]     build_components drivers/acpi
lappend_if [have_spec pci]      build_components drivers/pci/device_pd
lappend_if $use_usb             build_components drivers/usb
lappend_if $use_block_ide       build_components drivers/atapi
lappend_if $use_block_sata      build_components drivers/ahci
lappend_if $use_nic_session     build_components drivers/nic
lappend_if $use_nic_bridge      build_components server/nic_bridge
lappend_if $use_framebuffer     build_components drivers/framebuffer
lappend_if $use_fancy_stuff     build_components server/nitpicker
lappend_if $use_fancy_stuff     build_components server/liquid_framebuffer
lappend_if $use_fancy_stuff     build_components app/launchpad
lappend_if $use_fancy_stuff     build_components server/nitlog

build $build_components

# write Seoul config file
set vm_cfg_fd [open "bin/vm_linux.cfg" w]
puts $vm_cfg_fd {<config colocate="1" >
	<machine>
		<mem start="0x0" end="0x9a000"/>
		<mem start="0x100000" end="0xfffff000"/>
		<!--<ioio/>-->
		<nullio io_base="0x80" />
		<pic io_base="0x20" elcr_base="0x4d0"/>
		<pic io_base="0xa0" irq="2" elcr_base="0x4d1"/>
		<pit io_base="0x40" irq="0"/>
		<scp io_port_a="0x92" io_port_b="0x61"/>
		<kbc io_base="0x60" irq_kbd="1" irq_aux="12"/>
		<keyb ps2_port="0" host_keyboard="0x10000"/>
		<mouse ps2_port="1" host_mouse="0x10001"/>
		<rtc io_base="0x70" irq="8"/>
		<serial io_base="0x3f8" irq="0x4" host_serial="0x4711"/>
		<hostsink host_dev="0x4712" buffer="80"/>
		<vga io_base="0x03c0" fb_size="4096" readonly="1"/>}

if {!$use_multiboot} {
	puts $vm_cfg_fd {
		<vbios_disk/>
	}
}

	puts $vm_cfg_fd {
		<vbios_keyboard/>
		<vbios_mem/>
		<vbios_time/>
		<vbios_reset/>}

if {$use_multiboot} {
	puts $vm_cfg_fd {
		<vbios_multiboot/>
	}
} 

puts $vm_cfg_fd {
		<msi/>
		<ioapic/>
		<pcihostbridge bus_num="0" bus_count="0x10" io_base="0xcf8"
		               mem_base="0xe0000000"/>
		<pmtimer io_port="0x8000"/>

		<vcpu/> <halifax/> <vbios/> <lapic/>}

if {!$use_multiboot} {
	puts $vm_cfg_fd {
		<ahci mem="0xe0800000" irq="14" bdf="0x30"/>
		<drive sigma0drive="0" controller="0" port="0"/>
	}
}

if {$use_nic_session} {
	puts $vm_cfg_fd {
		<intel82576vf/>
	}
}

puts $vm_cfg_fd {
	</machine>
	<multiboot>}

if {$use_multiboot} {
	puts $vm_cfg_fd $multiboot_files
}

puts $vm_cfg_fd {
	 </multiboot>
</config>}
close $vm_cfg_fd

create_boot_directory

#
# Generate Genode config
#

append config {
<config verbose="yes">
	<parent-provides>
		<service name="ROM"/>
		<service name="RAM"/>
		<service name="IRQ"/>
		<service name="IO_MEM"/>
		<service name="IO_PORT"/>
		<service name="CAP"/>
		<service name="PD"/>
		<service name="RM"/>
		<service name="CPU"/>
		<service name="LOG"/>
		<service name="SIGNAL"/>
	</parent-provides>
	<default-route>
		<any-service> <parent/> <any-child/> </any-service>
	</default-route>

	<start name="timer">
		<resource name="RAM" quantum="1M"/>
		<provides><service name="Timer"/></provides>
	</start>

	<start name="rtc_drv">
		<resource name="RAM" quantum="1M"/>
		<provides><service name="Rtc"/></provides>
	</start>}

append_if [expr !$use_usb] config {
	<start name="ps2_drv">
		<resource name="RAM" quantum="3M"/>
		<provides><service name="Input"/></provides>
		<route> <any-service><parent/></any-service> </route>
	</start> }

append_if $use_block_sata config {
	<start name="ahci">
		<resource name="RAM" quantum="1M" />
		<provides><service name="Block"/></provides>
	</start>}

append_if $use_block_ide config {
	<start name="atapi_drv">
		<resource name="RAM" quantum="1M" />
		<provides><service name="Block"/></provides>
		<config ata="yes" />
	</start>}

append_if [have_spec acpi] config {
	<start name="acpi">
		<resource name="RAM" quantum="8M"/>
		<binary name="acpi_drv"/>
		<provides>
			<service name="PCI"/>
			<service name="IRQ" />
		</provides>
		<route>
			<service name="PCI"> <any-child /> </service>
			<any-service> <parent/> <any-child /> </any-service>
		</route>
	</start>}

append_if [expr ![have_spec acpi] && [have_spec pci]] config {
	<start name="pci_drv">
		<resource name="RAM" quantum="3M"/>
		<provides><service name="PCI"/></provides>
		<route> <any-service><parent/></any-service> </route>
	</start>
}

append_if $use_nic_session config {
	<start name="nic_drv">
		<resource name="RAM" quantum="3M"/>
		<provides><service name="Nic"/></provides>
		<route> <any-service><any-child/><parent/></any-service> </route>
	</start>
}

append_if [expr $use_nic_session && $use_nic_bridge] config {
	<start name="nic_bridge">
		<resource name="RAM" quantum="4M"/>
		<provides><service name="Nic"/></provides>
		<route>
			<service name="Nic"><child name="nic_drv"/></service>
			<any-service><parent/></any-service>
		</route>
	</start>
}

append_if $use_framebuffer config {
	<start name="fb_drv">
		<resource name="RAM" quantum="8M"/>
		<provides><service name="Framebuffer"/></provides>
		<route>
			<service name="Timer"><child name="timer"/></service>
			<service name="PCI"><any-child/></service>
			<any-service><parent/></any-service>
		</route>
		<config width="1024" height="768" depth="16" buffered="yes"/>
	</start> }

if {!$use_fancy_stuff} {
append config {
	<start name="vancouver">
		<binary name="vancouver"/>}
append config "
		<resource name=\"RAM\" quantum=\"$memory_vmm_vm\"/>"
append config {
		<route>
			<service name="Timer"><child name="timer"/></service>}
append_if [expr $use_nic_session && $use_nic_bridge] config {
			<service name="Nic"> <child name="nic_bridge"/> </service>}
append_if [expr $use_nic_session && !$use_nic_bridge] config {
			<service name="Nic"> <child name="nic_drv"/> </service>}
append_if $use_framebuffer config {
			<service name="Framebuffer"><child name="fb_drv"/></service>}
append config {
			<service name="Rtc"><child name="rtc_drv"/></service>
			<any-service><parent/><any-child/></any-service>
		</route>
		<configfile name="vm_linux.cfg"/>
	</start> }
}

append_if $use_usb config {
	<start name="usb_drv">
		<resource name="RAM" quantum="7M"/>
		<provides><service name="Input"/></provides>
		<config uhci="yes" ehci="yes" xhci="yes">
			<hid/>
		</config>
	</start>}

append_if $use_fancy_stuff config {
	<start name="nitpicker">
		<resource name="RAM" quantum="8M"/>
		<route>
			<service name="Timer">      <child name="timer"/></service>
			<service name="Framebuffer"><child name="fb_drv"/></service>
			<any-service><parent/><any-child/></any-service>
		</route>
		<provides><service name="Nitpicker"/></provides>
	</start>
	<start name="launchpad">
		<resource name="RAM" quantum="64000M"/>
		<configfile name="launchpad-config"/>
		<route>
			<service name="Nic"><child name="nic_bridge"/></service>
			<any-service><any-child/><parent/></any-service>
		</route>
	</start>}

append config {
</config>}

#
# Generate Launchpad config file
#
if {$use_fancy_stuff} {
	set launchpad_cfg_fd [open "bin/launchpad-config" w]

	puts $launchpad_cfg_fd "<config>
	<launcher ram_quota=\"$memory_init\" name=\"init\">"

	puts $launchpad_cfg_fd {
		<config>
			<parent-provides>
				<service name="ROM"/>
				<service name="RAM"/>
				<service name="IRQ"/>
				<service name="IO_MEM"/>
				<service name="IO_PORT"/>
				<service name="CAP"/>
				<service name="PD"/>
				<service name="RM"/>
				<service name="CPU"/>
				<service name="LOG"/>
				<service name="SIGNAL"/>
				<service name="Nitpicker"/>
				<service name="Timer"/>
				<service name="Nic"/>
				<service name="Block"/>
				<service name="Rtc"/>
			</parent-provides>

			<start name="vancouver_fb">
				<binary name="liquid_fb"/>
				<resource name="RAM" quantum="16M"/>
				<provides>
					<service name="Framebuffer"/>
					<service name="Input"/>
				</provides>
				<route>
					<any-service><parent/></any-service>
				</route>
				<config xpos="10" ypos="10" width="800" height="600" animate="off"/>
			</start>

			<start name="vancouver">
				<binary name="vancouver"/>
				<resource name="RAM" quantum="256M"/>
				<route>
					<service name="Timer"><parent/></service>
					<service name="Nic"><parent/></service>
					<service name="Input"><child name="vancouver_fb"/></service>
					<service name="Framebuffer"><child name="vancouver_fb"/></service>
					<any-service><parent/></any-service>
				</route>
				<configfile name="vm_linux.cfg"/>
			</start>
		</config>
	</launcher>}
	puts $launchpad_cfg_fd {</config>}
	close $launchpad_cfg_fd
}

install_config $config

#
# Boot modules
#

# generic modules
set boot_modules {
	core init
	pci_drv
	timer
	rtc_drv
	vancouver
	vm_linux.cfg
}

lappend_if [have_spec acpi]   boot_modules acpi_drv
lappend_if [have_spec nova]   boot_modules pci_device_pd
lappend_if [expr !$use_usb]   boot_modules ps2_drv
lappend_if $use_usb           boot_modules usb_drv
lappend_if $use_block_ide     boot_modules atapi_drv
lappend_if $use_block_sata    boot_modules ahci
lappend_if $use_nic_session   boot_modules nic_drv
lappend_if $use_nic_bridge    boot_modules nic_bridge
lappend_if $use_framebuffer   boot_modules fb_drv
lappend_if $use_fancy_stuff   boot_modules nitpicker
lappend_if $use_fancy_stuff   boot_modules liquid_fb
lappend_if $use_fancy_stuff   boot_modules launchpad
lappend_if $use_fancy_stuff   boot_modules nitlog
lappend_if $use_fancy_stuff   boot_modules launchpad-config


#
# Add OS binaries of guest
#

if {$use_multiboot} {
	set guest_os_binary_missing 0
	set binary_counter 0
	foreach binary $guest_os_binaries {
		if {![file exists bin/$binary]} {
			puts stderr "Error: guest OS binary \"bin/$binary\" does not exist"
			set guest_os_binary_missing 1
		}

		set sha1 [exec sha1sum bin/$binary]
		set sha1 [regexp -inline {[0-9a-h]+} $sha1]
		if {[string compare $sha1 [lindex $sha1_os_binaries $binary_counter]]} {
			puts "SHA1 sum of binary does not match with expected one - abort"
			puts "$binary $sha1 != [lindex $sha1_os_binaries $binary_counter]"
			set guest_os_binary_missing 1
		}
		incr binary_counter 1
	}

	if {$guest_os_binary_missing} { exit 1 }

	append boot_modules $guest_os_binaries
}

if {$use_usb} {
	append qemu_args " -usb -usbdevice mouse -usbdevice keyboard "
}

build_boot_image $boot_modules

# A copy of the config is placed in the run folder.
exec rm -f bin/vm_linux.cfg
