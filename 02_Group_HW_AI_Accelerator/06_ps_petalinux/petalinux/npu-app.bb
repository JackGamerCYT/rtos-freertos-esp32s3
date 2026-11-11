# Recipe PetaLinux: petalinux-create -t apps --template c --name npu-app --enable
# rồi chép npu_app.c, npu_drv.[ch], npu_ref.[ch], model_params.h, test_vectors.h vào files/
SUMMARY = "NPU DM-SA demo app"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"
SRC_URI = "file://npu_app.c file://npu_drv.c file://npu_drv.h file://npu_ref.c file://npu_ref.h \
           file://model_params.h file://test_vectors.h"
S = "${WORKDIR}"
do_compile() {
    sed -i 's#../03_golden_c/##' npu_app.c npu_drv.c
    ${CC} ${CFLAGS} ${LDFLAGS} -O2 -o npu_app npu_app.c npu_drv.c npu_ref.c
}
do_install() {
    install -d ${D}${bindir}
    install -m 0755 npu_app ${D}${bindir}
}
