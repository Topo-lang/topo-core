#!/bin/sh
# Fixed source dependencies; invoked only by the unprivileged build job.
set -eu
if [ "$#" -ne 2 ]; then echo 'usage: bootstrap.sh WORK_DIR PREFIX' >&2; exit 2; fi
work_dir=$1
prefix_dir=$2
python3 -I "$(dirname "$0")/candidate.py" fetch-dependencies "$work_dir"
mkdir -p "$prefix_dir"
cmake -S "$work_dir/json-3.11.3" -B "$work_dir/json-build" -DJSON_BuildTests=OFF -DCMAKE_INSTALL_PREFIX="$prefix_dir"
cmake --install "$work_dir/json-build"
mkdir -p "$prefix_dir/include"
cp -R "$work_dir/tomlplusplus-3.4.0/include/toml++" "$prefix_dir/include/"
