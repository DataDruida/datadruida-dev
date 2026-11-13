#!/usr/bin/env bash
# Compila el firmware DataDruida (MicroPython + LVGL + ddhw) para ESP32-S3.
# Licencia Apache-2.0 — DataDruida
#
# Corre dentro del contenedor espressif/idf:v5.2.2 (así lo usa el workflow
# .github/workflows/firmware.yml). Deja en firmware/dist/:
#   dd-lvgl-esp32s3.bin   imagen completa desde 0x0 (para ESP Web Tools)
#
#   ./firmware/build.sh            (con ESP-IDF 5.2.2 ya cargado: . $IDF_PATH/export.sh)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="${WORK:-$HERE/.work}"
# lv_micropython: MicroPython 1.24.1 + binding LVGL 9.3, combinación probada por LVGL
LVMP_REPO="https://github.com/lvgl/lv_micropython.git"
LVMP_SHA="44aa2b153dd3aa1ba183d05bd867fe3ffe2a1d75"

mkdir -p "$WORK" "$HERE/dist"
if [ ! -d "$WORK/lv_micropython/.git" ]; then
    git clone --filter=blob:none "$LVMP_REPO" "$WORK/lv_micropython"
fi
cd "$WORK/lv_micropython"
git fetch --quiet origin "$LVMP_SHA" || true
git checkout --quiet "$LVMP_SHA"
git submodule update --init --depth 1 user_modules/lv_binding_micropython
(cd user_modules/lv_binding_micropython && git submodule update --init --depth 1 lvgl pycparser)

# Fuentes grandes para las pantallas de 800x480 / 1024x600 (el lv_conf.h del
# binding sólo trae 14/16/24). Se parchea la copia de trabajo, fijada por SHA.
LV_CONF="user_modules/lv_binding_micropython/lv_conf.h"
sed -i -E 's/^#define LV_FONT_MONTSERRAT_(20|28|32|40|48) +0/#define LV_FONT_MONTSERRAT_\1 1/' "$LV_CONF"
grep -E '^#define LV_FONT_MONTSERRAT_(20|28|32|40|48) +1' "$LV_CONF" | wc -l | grep -q 5

make -C mpy-cross -j"$(nproc)"
make -C ports/esp32 BOARD_DIR="$HERE/boards/DD_S3_LVGL" submodules

make -C ports/esp32 -j"$(nproc)" \
    BOARD_DIR="$HERE/boards/DD_S3_LVGL" \
    USER_C_MODULES="$HERE/modules/ddhw/micropython.cmake"

BUILD="ports/esp32/build-DD_S3_LVGL"
python -m esptool --chip esp32s3 merge_bin -o "$HERE/dist/dd-lvgl-esp32s3.bin" \
    --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x0 "$BUILD/bootloader/bootloader.bin" \
    0x8000 "$BUILD/partition_table/partition-table.bin" \
    0x10000 "$BUILD/micropython.bin"

ls -la "$HERE/dist"
