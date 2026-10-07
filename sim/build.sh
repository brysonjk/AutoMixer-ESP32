#!/bin/bash
# Builds the browser simulator: the firmware's real main.cpp, gui.cpp, sensors.cpp and
# valve_control.cpp plus LVGL, compiled with Emscripten against the stand-ins in sim/.
#   bash sim/build.sh        ->  sim/build/auto-mixer-sim.js
set -e
cd "$(dirname "$0")/.."
LVGL=.pio/libdeps/esp32-s3-devkitc-1/lvgl
OUT=sim/build
mkdir -p "$OUT/obj"

INC="-Isim/include -Iinclude -I$LVGL -DLV_CONF_INCLUDE_SIMPLE"
OPT="-O2"

# LVGL once (slow); reused until its sources or lv_conf.h change.
if [ ! -f "$OUT/liblvgl.a" ] || [ include/lv_conf.h -nt "$OUT/liblvgl.a" ]; then
  echo "Compiling LVGL..."
  rm -f "$OUT"/obj/lv_*.o
  for f in $(find "$LVGL/src" -name "*.c"); do
    o="$OUT/obj/lv_$(echo "$f" | sed 's|.*/src/||; s|/|_|g; s|\.c$|.o|')"
    emcc $OPT $INC -c "$f" -o "$o"
  done
  emar rcs "$OUT/liblvgl.a" "$OUT"/obj/lv_*.o
fi

echo "Compiling firmware and simulator..."
FIRMWARE="src/main.cpp src/gui.cpp src/sensors.cpp src/valve_control.cpp"
SIM="sim/src/sim_platform.cpp sim/src/sim_world.cpp sim/src/sim_display.cpp sim/src/sim_network.cpp sim/src/sim_main.cpp"

# No WebAssembly: plain JavaScript, so it also runs where a page's security policy
# refuses to compile wasm.
em++ $OPT -std=gnu++17 $INC $FIRMWARE $SIM "$OUT/liblvgl.a" -o "$OUT/auto-mixer-sim.js" \
  -sWASM=0 -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 \
  -sMODULARIZE=1 -sEXPORT_NAME=createAutoMixerSim \
  -sEXPORTED_FUNCTIONS=_main,_sim_pointer,_sim_set_compressor,_sim_compressor,_sim_gas
echo "Built $OUT/auto-mixer-sim.js ($(du -h "$OUT/auto-mixer-sim.js" | cut -f1))"

# One self-contained page: the template with the compiled firmware inlined.
python3 - <<'PY'
tpl = open('sim/web/page-template.html').read()
js = open('sim/build/auto-mixer-sim.js').read()
open('sim/build/auto-mixer-live-demo.html', 'w').write(tpl.replace('/*SIM_JS*/', js))
PY
echo "Built sim/build/auto-mixer-live-demo.html ($(du -h sim/build/auto-mixer-live-demo.html | cut -f1))"
