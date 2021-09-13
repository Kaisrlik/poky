SUMMARY = "Rust testing"
HOMEPAGE = "https://rustc-dev-guide.rust-lang.org/tests/intro.html"
SECTION = "test"
LICENSE = "MIT | Apache-2.0"

inherit rust
inherit cargo_common

DEPENDS += "file-native python3-native"
EXCLUDE_FROM_WORLD = "1"

S = "${RUSTSRC}"
# Path of target specification file "target-poky-linux.json"
export RUST_TARGET_PATH="${STAGING_LIBDIR_NATIVE}/rustlib"

export FORCE_CRATE_HASH="${BB_TASKHASH}"

# We don't want to use bitbakes vendoring because the rust sources do their
# own vendoring.
CARGO_DISABLE_BITBAKE_VENDORING = "1"

# We can't use RUST_BUILD_SYS here because that may be "musl" if
# TCLIBC="musl". Snapshots are always -unknown-linux-gnu
SNAPSHOT_BUILD_SYS = "${BUILD_ARCH}-unknown-linux-gnu"
setup_cargo_environment () {
    # The first step is to build bootstrap and some early stage tools,
    # these are build for the same target as the snapshot, e.g.
    # x86_64-unknown-linux-gnu.
    # Later stages are build for the native target (i.e. target.x86_64-linux)
    cargo_common_do_configure

    printf '[target.%s]\n' "${SNAPSHOT_BUILD_SYS}" >> ${CARGO_HOME}/config
    printf "linker = '%s'\n" "${RUST_BUILD_CCLD}" >> ${CARGO_HOME}/config
}

include rust-common.inc
do_rust_setup_snapshot () {
    for installer in "${WORKDIR}/rust-snapshot-components/"*"/install.sh"; do
        "${installer}" --prefix="${WORKDIR}/rust-snapshot" --disable-ldconfig
    done

    # Some versions of rust (e.g. 1.18.0) tries to find cargo in stage0/bin/cargo
    # and fail without it there.
    mkdir -p ${RUSTSRC}/build/${BUILD_SYS}
    ln -sf ${WORKDIR}/rust-snapshot/ ${RUSTSRC}/build/${BUILD_SYS}/stage0
}
addtask rust_setup_snapshot after do_unpack before do_configure
do_rust_setup_snapshot[dirs] += "${WORKDIR}/rust-snapshot"

python do_configure() {
    import json
    from distutils.version import LooseVersion
    try:
        import configparser
    except ImportError:
        import ConfigParser as configparser

    # toml is rather similar to standard ini like format except it likes values
    # that look more JSON like. So for our purposes simply escaping all values
    # as JSON seem to work fine.

    e = lambda s: json.dumps(s)

    config = configparser.RawConfigParser()

    # [target.ARCH-unknown-linux-gnu] in case of x86_64 [target.ARCH-poky-linux]
    target_section = "target.{}".format(d.getVar('TARGET_SYS', True))
    config.add_section(target_section)

    # Points to wrapper files which contain target specific compiler and linker commands.
    config.set(target_section, "cxx", e(d.expand("${RUST_TARGET_CXX}")))
    config.set(target_section, "cc", e(d.expand("${RUST_TARGET_CC}")))
    config.set(target_section, "linker", e(d.expand("${RUST_TARGET_CCLD}")))

    # If we don't do this rust-native will compile it's own llvm for BUILD.
    # [target.${BUILD_ARCH}-unknown-linux-gnu]
    target_section = "target.{}".format(d.getVar('SNAPSHOT_BUILD_SYS', True))
    config.add_section(target_section)

    # Wrapper scripts of build system.
    config.set(target_section, "cxx", e(d.expand("${RUST_BUILD_CXX}")))
    config.set(target_section, "cc", e(d.expand("${RUST_BUILD_CC}")))

    # [llvm]
    config.add_section("llvm")
    config.set("llvm", "targets", e("ARM;AArch64;Mips;PowerPC;RISCV;X86"))

    # [rust]
    config.add_section("rust")
    config.set("rust", "rpath", e(True))
    config.set("rust", "channel", e("stable"))

    if LooseVersion(d.getVar("PV")) < LooseVersion("1.32.0"):
        config.set("rust", "use-jemalloc", e(False))

    # Whether or not to optimize the compiler and standard library
    config.set("rust", "optimize", e(True))

    # Emits extraneous output from tests to ensure that failures of the test
    # harness are debuggable just from logfiles
    config.set("rust", "verbose-tests", e(True))

    # Override default linker cc.
    config.set("rust", "default-linker", e(d.expand("${RUST_BUILD_CCLD}")))

    # [build]
    config.add_section("build")
    config.set("build", "submodules", e(False))
    config.set("build", "docs", e(False))

    rustc = d.expand("${WORKDIR}/rust-snapshot/bin/rustc")
    config.set("build", "rustc", e(rustc))

    cargo = d.expand("${WORKDIR}/rust-snapshot/bin/cargo")
    config.set("build", "cargo", e(cargo))

    config.set("build", "vendor", e(True))

    targets = [d.getVar("TARGET_SYS", True)]
    config.set("build", "target", e(targets))

    hosts = [d.getVar("SNAPSHOT_BUILD_SYS", True)]
    config.set("build", "host", e(hosts))

    # We can't use BUILD_SYS since that is something the rust snapshot knows
    # nothing about when trying to build some stage0 tools (like fabricate)
    config.set("build", "build", e(d.getVar("SNAPSHOT_BUILD_SYS", True)))

    with open("config.toml", "w") as f:
        config.write(f)

    # set up ${WORKDIR}/cargo_home
    bb.build.exec_func("setup_cargo_environment", d)
}


rust_runx () {
    echo "COMPILE ${PN}" "$@"

    # CFLAGS, LDFLAGS, CXXFLAGS, CPPFLAGS are used by rust's build for a
    # wide range of targets (not just TARGET). Yocto's settings for them will
    # be inappropriate, avoid using.
    unset CFLAGS
    unset LDFLAGS
    unset CXXFLAGS
    unset CPPFLAGS

    oe_cargo_fix_env

    python3 src/bootstrap/bootstrap.py ${@oe.utils.parallel_make_argument(d, '-j %d')} "$@" --verbose
}
rust_runx[vardepsexclude] += "PARALLEL_MAKE"

do_compile () {

    rust_runx build src/tools/remote-test-server --target "${TARGET_SYS}"
}
