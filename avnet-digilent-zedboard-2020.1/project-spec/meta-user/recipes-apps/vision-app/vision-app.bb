#
# This file is the vision-app recipe.
#

SUMMARY = "Cool vision-app application"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
DEPENDS += "opencv"
RDEPENDS_${PN} += "libopencv-core libopencv-highgui libopencv-imgproc opencv"

SRC_URI = "file://vision-app.cpp \
           file://Makefile \
		  "

S = "${WORKDIR}"

do_compile() {
	     oe_runmake
}

do_install() {
	     install -d ${D}${bindir}
	     install -m 0755 vision-app ${D}${bindir}
}
