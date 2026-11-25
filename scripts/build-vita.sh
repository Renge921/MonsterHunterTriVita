#!/usr/bin/env bash
# Build the PS Vita package: $MH3_VITA_BUILD_DIR/mh3-vita.vpk (default
# build/vita). A release build (ThinLTO, the checked-in PGO profile) unless
# MH3_DEVELOPMENT=ON; MH3_EXTRA_CMAKE_ARGS adds CMake options. PSP2CGC is the
# absolute path to Sony's psp2cgc.exe.
set -euo pipefail
cd "$(dirname "$0")/.."
task_build_dir=${MH3_VITA_BUILD_DIR:-build/vita}
task_sdk=${VITASDK:-/usr/local/vitasdk}
export VITASDK="$task_sdk"
export DOTNET_BIN="${DOTNET_BIN:-$(which dotnet)}"
task_args=()
# MH3_VITA_ELF_CREATE: another vita-elf-create than the SDK's.
[[ -n ${MH3_VITA_ELF_CREATE:-} ]] && task_args+=("-DVITA_ELF_CREATE:PATH=$MH3_VITA_ELF_CREATE")
# Reuse another build's dependency downloads when there is one.
for task_dependency in sdl xxhash fmt imgui sqlite3 zstd; do
  task_source="$PWD/build/aurora-arm64/_deps/$task_dependency-src"
  if [[ -d "$task_source" ]]; then
    task_args+=("-DFETCHCONTENT_SOURCE_DIR_${task_dependency^^}=$task_source")
  fi
done
cmake -S runtime -B "$task_build_dir" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/toolchains/vita-clang.cmake" \
  -DVITASDK="$task_sdk" -DCMAKE_BUILD_TYPE=Release \
  -DMH3_AURORA_ROOT="$PWD/aurora-main" \
  -DMH3_DEVELOPMENT="${MH3_DEVELOPMENT:-OFF}" \
  -DMH3_PGO_RELEASE_PROFILE="${MH3_PGO_RELEASE_PROFILE:-OFF}" \
  -DAURORA_GXM=ON -DAURORA_ENABLE_CARD=OFF \
  -DAURORA_SDL3_PROVIDER=vendor -DAURORA_SDL3_LINKAGE=static \
  -DBUILD_SHARED_LIBS=OFF "${task_args[@]}" ${MH3_EXTRA_CMAKE_ARGS:-}
cmake --build "$task_build_dir" --target eboot.bin --parallel "${BUILD_JOBS:-$(nproc)}"
"$task_sdk/bin/vita-mksfoex" -d ATTRIBUTE2=12 -s APP_VER=00.01 -s TITLE_ID=MH3WII001 "Monster Hunter Tri" "$task_build_dir/mh3-vita.vpk_param.sfo"
"$task_sdk/bin/vita-pack-vpk" \
  -a "$PWD/runtime/sce_sys/icon0.png=sce_sys/icon0.png" \
  -a "$PWD/runtime/sce_sys/livearea/contents/bg0.png=sce_sys/livearea/contents/bg0.png" \
  -a "$PWD/runtime/sce_sys/livearea/contents/startup.png=sce_sys/livearea/contents/startup.png" \
  -a "$PWD/runtime/sce_sys/livearea/contents/template.xml=sce_sys/livearea/contents/template.xml" \
  -s "$task_build_dir/mh3-vita.vpk_param.sfo" \
  -b "$task_build_dir/eboot.bin" \
  "$task_build_dir/mh3-vita.vpk"
cp -f "$task_build_dir/mh3-vita.vpk" "$task_build_dir/mh3-vita.vpk.out"
echo "Built $task_build_dir/mh3-vita.vpk"
