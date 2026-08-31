#!/usr/bin/env bash
#
# Configure, build and install KDepends into ~/.local.
#
#   ./install-local.sh              # optimised release build
#   ./install-local.sh --clean      # discard the build directory first
#
# The build type is deliberately not configurable: this script exists to install
# the copy you actually run, so it is always an optimised release. Use the
# build/ tree that development-guide.md documents for debug work. That is also
# why this uses its own build directory. The ~/.local prefix set here must
# never clobber the /usr configuration in build/.

set -euo pipefail

SOURCE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-${SOURCE_DIR}/build-local}"
BUILD_TYPE="Release"

PREFIX="${HOME}/.local"
BIN_DIR="${PREFIX}/bin"
APP_DIR="${PREFIX}/share/applications"
ICON_DIR="${PREFIX}/share/icons"
DESKTOP_FILE="kdepends.desktop"
ICON_SOURCE="src/icons/sc-apps-kdepends.svg"

case "${1:-}" in
	--clean)
		rm -rf -- "${BUILD_DIR}"
		;;
	"")
		;;
	*)
		echo "usage: ${0##*/} [--clean]" >&2
		exit 2
		;;
esac

# Release gives -O3 -DNDEBUG. Link-time optimisation is worthwhile here
# because the analysis core is split across many small translation units whose
# hot paths only inline across them with LTO.
echo "==> Configuring ${BUILD_TYPE} build, prefix ${PREFIX}"
cmake -B "${BUILD_DIR}" -S "${SOURCE_DIR}" -G Ninja \
	-DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
	-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
	-DCMAKE_INSTALL_PREFIX="${PREFIX}"

echo "==> Building"
cmake --build "${BUILD_DIR}"

echo "==> Installing to ${PREFIX}"
cmake --install "${BUILD_DIR}" --strip

# The install rules already place this, but do it explicitly so the entry is
# guaranteed to be in the user's applications directory and readable.
echo "==> Installing ${DESKTOP_FILE} to ${APP_DIR}"
mkdir -p -- "${APP_DIR}"
install -m 0644 -- "${SOURCE_DIR}/src/${DESKTOP_FILE}" "${APP_DIR}/${DESKTOP_FILE}"

if command -v update-desktop-database >/dev/null 2>&1
then
	update-desktop-database -q -- "${APP_DIR}" || true
fi

# Icon lookup treats loose files in the root of ~/.local/share/icons as a
# last-resort match for Icon=kdepends, and unlike the hicolor tree it needs no
# cache update, so the icon shows up immediately.
echo "==> Installing kdepends.svg to ${ICON_DIR}"
mkdir -p -- "${ICON_DIR}"
install -m 0644 -- "${SOURCE_DIR}/${ICON_SOURCE}" "${ICON_DIR}/kdepends.svg"

# The desktop portal resolves the app from the desktop entry *and* from its Exec
# name being findable on PATH. Without both it logs an "App info not found"
# warning on every launch.
case ":${PATH}:" in
	*":${BIN_DIR}:"*)
		;;
	*)
		echo
		echo "warning: ${BIN_DIR} is not on your PATH."
		echo "         kdepends will log a desktop portal warning at launch until it is."
		;;
esac

echo
echo "Installed:"
echo "  ${BIN_DIR}/kdepends"
echo "  ${APP_DIR}/${DESKTOP_FILE}"
echo "  ${ICON_DIR}/kdepends.svg"
