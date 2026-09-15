#!/bin/zsh
# Build the example with ESP-IDF 5.4.2: ./build.sh [extra idf.py args]
source "$HOME/.espressif/tools/activate_idf_v5.4.2.sh" >/dev/null 2>&1
cd "$(dirname "$0")/examples/minimal" || exit 1
exec "$IDF_PYTHON_ENV_PATH/bin/python" "$IDF_PATH/tools/idf.py" "$@" build
