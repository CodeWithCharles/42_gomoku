#!/bin/sh
cd "$(dirname $0)"

# --------------------------------- Defaults --------------------------------- #
make_full_log=
debug=
sanitize=
optimize=y
native=y
objdir=build
outdir=..
cxxflags=
ldflags=

usage() {
cat <<EOF
Usage: $0 [OPTION]
General options:
  --help                prints help
  --make-full-log       prints complete commands during build
Build options:
  --debug               activate debug mode (-g3 -O0)
  --sanitize            enable AddressSanitizer + UndefinedBehaviorSanitizer
  --optimize-disable    disable -O3
  --native-disable      disable -march=native (needed if build and run hosts differ)
  --objdir=OBJDIR       directory for all objects (default: ./build)
  --outdir=OUTDIR       directory for all output binaries (default: .., the repo root)
Other tweaks:
  --cxxflags=FLAGS      some more compilation flags
  --ldflags=FLAGS       some more linker flags
EOF
exit 0
}

for arg ; do case "$arg" in --help|-h) usage ;; esac; done

for arg ; do case "$arg" in
--make-full-log)    make_full_log=y ;;
--debug)            debug=y ;;
--sanitize)         sanitize=y ;;
--optimize-disable) optimize= ;;
--native-disable)   native= ;;
--objdir=*)         objdir="${arg#*=}" ;;
--outdir=*)         outdir="${arg#*=}" ;;
--cxxflags=*)       cxxflags="${arg#*=}" ;;
--ldflags=*)        ldflags="${arg#*=}" ;;
*) echo "Unknown option: $arg"; exit 1 ;;
esac; done

# Seulement si une config existe deja : sinon `make` appellerait ce script,
# qui rappellerait `make`, qui ne trouverait toujours pas Makefile.cfg.
if [ -f Makefile.cfg ]; then
	make mrproper MAKE_FULL_LOG=y 1>/dev/null 2>/dev/null
fi

exec 3>&1 1>Makefile.cfg
cat <<EOF
#!/usr/bin/make -f
# ---------------------------------------------------------------------------- #
#                        ./configure.sh generated config                       #
# ---------------------------------------------------------------------------- #
MAKE_FULL_LOG	= $make_full_log
DEBUG			:= $debug
SANITIZE		:= $sanitize
OPTIMIZE		:= $optimize
NATIVE			:= $native
OBJDIR			:= $objdir
OUTDIR			:= $outdir
CXXMOREFLAGS	:= $cxxflags
LDMOREFLAGS	:= $ldflags
# End of file
EOF
exec 1>&3 3>&-

chmod +x Makefile.cfg
echo "Wrote configuration, you can 'make' now."