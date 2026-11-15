SUMMARY = "atcd and its boot-confirm guard"
DESCRIPTION = "Installs the atcd A/B boot-confirm guard and atcd, a pinned \
release fetched by its sha256. The recipe does NOT install a systemd unit: the \
unit interpolates the store root, socket path, service user and telemetry \
endpoint of the specific device, so a copy baked into an image drifts from \
render_systemd_unit. First-boot `atc bootstrap atcd install` renders it."
HOMEPAGE = "https://github.com/Christian-Prather/meta-atc"
# The layer's files are MIT. atcd and the guard come from the atc source, which
# is MIT OR Apache-2.0.
LICENSE = "MIT | Apache-2.0"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

# atcd is the static musl binary of the atc release. bitbake does not rebuild
# it, so the image carries the same bytes the fleet runs.
INHIBIT_PACKAGE_STRIP = "1"
INHIBIT_PACKAGE_DEBUG_SPLIT = "1"
INHIBIT_SYSROOT_STRIP = "1"

# atc publishes release targets for these two architectures only.
COMPATIBLE_HOST = "(aarch64|x86_64).*-linux.*"

# The release target for this build's architecture.
ATCD_TARGET = ""
ATCD_TARGET:x86-64 = "linux-x86_64-musl"
ATCD_TARGET:aarch64 = "linux-aarch64-musl"

# The release: PV, from the name of the including atcd_<version>.bb, and the
# sha256 of each target's binary, which that file sets in ATCD_SHA256:aarch64
# and ATCD_SHA256:x86-64. atc.dev serves release blobs by their sha256, so the
# URL and the checksum name the same bytes, and do_fetch refuses any other. The
# digests come from the SIGNED release index: scripts/install-atcd.sh checks
# the signature when they are pinned. README, "atcd version", shows how to add
# a release. The fetch is an ordinary do_fetch: the download is cached,
# mirrored and reused from sstate, and a build without network works from
# DL_DIR.
ATCD_SHA256 = ""
ATCD_BLOB = "atcd-${PV}-${ATCD_TARGET}"

# The guard is a copy of atc's own script. The atc maintainers keep it
# byte-for-byte equal to the original.
SRC_URI = " \
    file://atcd-boot-confirm.sh \
    https://atc.dev/agency/blobs/sha256/${ATCD_SHA256};downloadfilename=${ATCD_BLOB};name=atcd \
    "
SRC_URI[atcd.sha256sum] = "${ATCD_SHA256}"

# The ELF machine name readelf must report for this target. A recipe that adds a
# machine must add its expected value here, or do_install refuses the binary.
ATCD_EXPECTED_ELF_MACHINE ?= ""
ATCD_EXPECTED_ELF_MACHINE:qemux86-64 = "Advanced Micro Devices X86-64"
ATCD_EXPECTED_ELF_MACHINE:qemuarm64 = "AArch64"
ATCD_EXPECTED_ELF_MACHINE:tegra234 = "AArch64"

# Paths the running system uses. They must match what atcd expects:
#   libexec directory  /usr/local/libexec
#   guard              /usr/local/libexec/atcd-boot-confirm.sh
#   remote_path        /usr/local/bin/atcd, a symlink that bootstrap makes
ATCD_LIBEXEC = "/usr/local/libexec"
ATCD_GUARD_PATH = "${ATCD_LIBEXEC}/atcd-boot-confirm.sh"
ATCD_REMOTE_PATH = "${bindir}/atcd"

S = "${WORKDIR}"

# Only the guard is arch-independent. The binary makes the package
# machine-specific, so the whole package is.
PACKAGE_ARCH = "${MACHINE_ARCH}"

# readelf for the architecture check.
DEPENDS += "binutils-native"

do_configure[noexec] = "1"
do_compile[noexec] = "1"

do_install() {
    install -d ${D}${ATCD_LIBEXEC}
    install -m 0755 ${WORKDIR}/atcd-boot-confirm.sh ${D}${ATCD_GUARD_PATH}

    if [ ! -f "${WORKDIR}/${ATCD_BLOB}" ]; then
        bbfatal "no atcd at ${WORKDIR}/${ATCD_BLOB}. do_fetch did not fetch it."
    fi

    if [ -z "${ATCD_EXPECTED_ELF_MACHINE}" ]; then
        bbfatal "No ATCD_EXPECTED_ELF_MACHINE for MACHINE=${MACHINE}. \
Add the readelf machine name for this target before installing a binary, \
or a wrong-architecture ELF installs silently."
    fi

    # The architecture assertion. `readelf -h` prints one "Machine:" line; a
    # binary for the wrong target reports a different name and fails the build
    # here rather than at first boot on the device.
    #
    # The NATIVE (unprefixed) readelf from binutils-native, deliberately. It
    # reads any ELF header regardless of target arch, and reading a header is
    # the whole job here. A `${TARGET_PREFIX}readelf` would need
    # binutils-cross-${TARGET_ARCH}, which this recipe does not depend on, and
    # a missing command fails OPEN: the substitution yields an empty string,
    # which then trips the mismatch branch with a confusing message instead of
    # reporting the real problem.
    if ! actual_machine=$(readelf -h "${WORKDIR}/${ATCD_BLOB}" 2>&1); then
        bbfatal "readelf could not read ${WORKDIR}/${ATCD_BLOB}: ${actual_machine}"
    fi
    actual_machine=$(printf '%s\n' "${actual_machine}" \
        | sed -n 's/^[[:space:]]*Machine:[[:space:]]*//p')
    if [ -z "${actual_machine}" ]; then
        bbfatal "readelf printed no Machine line for ${WORKDIR}/${ATCD_BLOB}; \
refusing to install a binary whose architecture cannot be confirmed."
    fi
    if [ "${actual_machine}" != "${ATCD_EXPECTED_ELF_MACHINE}" ]; then
        bbfatal "atcd architecture mismatch for MACHINE=${MACHINE}: \
readelf reports '${actual_machine}', expected '${ATCD_EXPECTED_ELF_MACHINE}'."
    fi

    install -d ${D}${bindir}
    install -m 0755 "${WORKDIR}/${ATCD_BLOB}" ${D}${ATCD_REMOTE_PATH}
}

# Deliberately NOT created here. `atcd install-self` creates /var/atc with the
# modes the traverse-grant walk expects; a hardened image umask of 077 otherwise
# produces 0700 directories and breaks `atcd attach` with EACCES.
FILES:${PN} = "${ATCD_GUARD_PATH} ${ATCD_REMOTE_PATH}"

# The release binary is a static musl ELF: it has no dynamic dependencies for
# bitbake to resolve, and it is not built by this recipe's toolchain.
INSANE_SKIP:${PN} += "already-stripped arch staticdev ldflags"
