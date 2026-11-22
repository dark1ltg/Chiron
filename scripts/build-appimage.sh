#!/usr/bin/env bash
# Build a Linux x86_64 Chiron AppImage using upstream's static Qt toolchain.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

TOOLS_URL="${TOOLS_URL:-https://github.com/mmatyas/pegasus-frontend/releases/download/alpha1}"
QT_VERSION="${QT_VERSION:-qt51512}"
QT_TARGET="${QT_TARGET:-x11-static}"
SDL_VERSION="${SDL_VERSION:-2.32.10}"
SDL_TARGET="${SDL_TARGET:-x11-jammy}"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-appimage}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
APPDIR="${APPDIR:-$BUILD_DIR/AppDir}"
JOBS="${JOBS:-$(nproc)}"

QT_PREFIX="/opt/${QT_VERSION}_${QT_TARGET}"
SDL_PREFIX="/opt/SDL2-${SDL_VERSION}"

log() { printf '==> %s\n' "$*"; }

need_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "missing required command: $1" >&2
    exit 1
  }
}

need_cmd curl
need_cmd cmake
need_cmd ninja
need_cmd g++
need_cmd file

fetch_tooling() {
  if [[ ! -x "${QT_PREFIX}/bin/qmake" ]]; then
    log "Downloading ${QT_VERSION}_${QT_TARGET}"
    curl -fL "${TOOLS_URL}/${QT_VERSION}_${QT_TARGET}.tar.xz" | sudo tar xJf - -C /opt/
  else
    log "Reusing ${QT_PREFIX}"
  fi

  if [[ ! -d "${QT_PREFIX}/include/QtGui" ]] && [[ -f /opt/apng_patched_headers.tar.xz || true ]]; then
    if curl -fL "${TOOLS_URL}/apng_patched_headers.tar.xz" -o /tmp/apng_patched_headers.tar.xz; then
      sudo tar xJf /tmp/apng_patched_headers.tar.xz -C "${QT_PREFIX}/include/" || true
    fi
  fi

  if [[ ! -f "${SDL_PREFIX}/lib/libSDL2.a" && ! -f "${SDL_PREFIX}/lib/libSDL2.so" ]]; then
    log "Downloading SDL2-${SDL_VERSION}_${SDL_TARGET}"
    curl -fL "${TOOLS_URL}/SDL2-${SDL_VERSION}_${SDL_TARGET}.tar.xz" | sudo tar xJf - -C /opt/
  else
    log "Reusing ${SDL_PREFIX}"
  fi
}

fetch_linuxdeploy() {
  local tools="$BUILD_DIR/tools"
  mkdir -p "$tools"
  if [[ ! -x "$tools/linuxdeploy-x86_64.AppImage" ]]; then
    log "Downloading linuxdeploy"
    curl -fL "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
      -o "$tools/linuxdeploy-x86_64.AppImage"
    chmod +x "$tools/linuxdeploy-x86_64.AppImage"
  fi
  if [[ ! -x "$tools/linuxdeploy-plugin-qt-x86_64.AppImage" ]]; then
    log "Downloading linuxdeploy Qt plugin"
    curl -fL "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" \
      -o "$tools/linuxdeploy-plugin-qt-x86_64.AppImage"
    chmod +x "$tools/linuxdeploy-plugin-qt-x86_64.AppImage"
  fi
  echo "$tools"
}

configure_and_build() {
  # Prefer GCC; some images default c++ to Clang without libstdc++.
  export CC="${CC:-gcc}"
  export CXX="${CXX:-g++}"

  log "Configuring CMake"
  cmake -S "$ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$CC" \
    -DCMAKE_CXX_COMPILER="$CXX" \
    -DCMAKE_PREFIX_PATH="${QT_PREFIX};${SDL_PREFIX}" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DPEGASUS_ENABLE_APNG=ON \
    -DPEGASUS_STATIC_CXX=ON \
    -DPEGASUS_ENABLE_LTO=OFF

  log "Building"
  cmake --build "$BUILD_DIR" -j"$JOBS"

  log "Installing into AppDir"
  rm -rf "$APPDIR"
  DESTDIR="$APPDIR" cmake --install "$BUILD_DIR"
}

# Static Qt AppImages still need a desktop entry whose Exec is a basename
# linuxdeploy can find inside usr/bin.
fix_desktop_for_appimage() {
  local desktop="$APPDIR/usr/share/applications/org.dark1ltg.Chiron.desktop"
  if [[ -f "$desktop" ]]; then
    sed -i 's|^Exec=.*|Exec=pegasus-fe|' "$desktop"
  fi

  # Prefer a larger icon at AppDir root for AppImage tooling
  local icon128="$APPDIR/usr/share/icons/hicolor/128x128/apps/org.dark1ltg.Chiron.png"
  if [[ -f "$icon128" ]]; then
    cp "$icon128" "$APPDIR/org.dark1ltg.Chiron.png"
  fi
}

resolve_version() {
  if [[ -n "${VERSION:-}" ]]; then
    printf '%s\n' "$VERSION"
    return
  fi
  local tag
  tag="$(git -C "$ROOT" describe --tags --exact-match 2>/dev/null || true)"
  if [[ -n "$tag" ]]; then
    printf '%s\n' "${tag#v}"
    return
  fi
  tag="$(git -C "$ROOT" describe --tags --match 'chiron-*' --abbrev=0 2>/dev/null || true)"
  if [[ -n "$tag" ]]; then
    local short
    short="$(git -C "$ROOT" rev-parse --short HEAD)"
    printf '%s-%s\n' "${tag#chiron-}" "$short"
    return
  fi
  printf '0.1.0-%s\n' "$(git -C "$ROOT" rev-parse --short HEAD)"
}

package_appimage() {
  local tools
  tools="$(fetch_linuxdeploy)"
  mkdir -p "$DIST_DIR"

  local version
  version="$(resolve_version)"
  local out_name="Chiron-${version}-x86_64.AppImage"

  log "Running linuxdeploy (version ${version})"
  export VERSION="$version"
  export LINUXDEPLOY_OUTPUT_VERSION="$version"
  export QMAKE="${QT_PREFIX}/bin/qmake"
  # Static Qt: plugin may no-op / warn; still useful for non-Qt libs + AppImage wrap.
  export APPIMAGE_EXTRACT_AND_RUN=1

  (
    cd "$BUILD_DIR"
    rm -f ./*.AppImage
    "$tools/linuxdeploy-x86_64.AppImage" \
      --appdir "$APPDIR" \
      --desktop-file "$APPDIR/usr/share/applications/org.dark1ltg.Chiron.desktop" \
      --icon-file "$APPDIR/org.dark1ltg.Chiron.png" \
      --executable "$APPDIR/usr/bin/pegasus-fe" \
      --output appimage
  )

  local produced
  produced="$(find "$BUILD_DIR" -maxdepth 1 -name 'Chiron-*.AppImage' -printf '%T@ %p\n' 2>/dev/null | sort -nr | awk 'NR==1{print $2}')"
  if [[ -z "${produced:-}" || ! -f "$produced" ]]; then
    echo "AppImage was not produced" >&2
    exit 1
  fi

  mv -f "$produced" "$DIST_DIR/$out_name"
  chmod +x "$DIST_DIR/$out_name"
  ln -sfn "$out_name" "$DIST_DIR/Chiron-x86_64.AppImage"
  file "$DIST_DIR/$out_name"
  ls -lh "$DIST_DIR/$out_name"
  log "AppImage ready: $DIST_DIR/$out_name"
}

main() {
  git -C "$ROOT" submodule update --init --recursive
  fetch_tooling
  configure_and_build
  fix_desktop_for_appimage
  package_appimage
}

main "$@"
