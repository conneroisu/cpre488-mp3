#
# This file is the launch-ctrl recipe.
#

SUMMARY = "Simple launch-ctrl application"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://launcher-fire-buttons.c \
	   file://Makefile \
	   file://launcher-commands.h \
	   file://controls/control-interface.c \
	   file://controls/control-interface.h \
		  "

S = "${WORKDIR}"

do_compile() {
	     oe_runmake
}

do_install() {
	     install -d ${D}${bindir}
	     install -m 0755 launch-ctrl ${D}${bindir}
}
