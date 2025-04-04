#!/usr/bin/env expect
set timeout -1
set install_dir [lindex $argv 0]
set version [lindex $argv 1]
set release [lindex $argv 2]

# Assign path of the installer to a variable
set installer_path /petalinux-v$version-$release-installer.run

# Check if the installer exists
if {![file exists $installer_path]} {
    # send_user "ERROR: PetaLinux installer not found. Please download it from AMD/Xilinx website.\r"
    # Print the installer path
    send_user "Installer path: $installer_path\r"

    exit 1
}

# Check if the installer has executable permission
if {![file executable $installer_path]} {
    send_user "ERROR: PetaLinux installer is not executable. Trying to set permissions.\r"
    exec chmod +x /petalinux-v$version-$release-installer.run
}

send_user "Starting PetaLinux $version installation in $install_dir\r"
spawn $installer_path --dir $install_dir
expect {
    "Press Enter to display the license agreements" {
        send "\r"
        send "\q"
        expect "*>*"
        send "y\r"
        send "\r"
        send "\q"
        expect "*>*"
        send "y\r"
        expect "*RETURN*"
        send "\r"
        send "\q"
        expect "*>*"
        send "y\r"
    }
    timeout {
        send_user "ERROR: Installation timed out\r"
        exit 1
    }
}

expect {
    "*Petalinux SDK has been installed to*" {
        send_user "Auto install PetaLinux done\r"
        send_user "Wait several minutes to clean up\r"
    }
    timeout {
        send_user "ERROR: Installation timed out while waiting for completion\r"
        exit 1
    }
}
exit
