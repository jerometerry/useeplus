#!/usr/bin/env bash
set -euo pipefail

# 1. Gather files matching the exact search targets from your Makefile
# Using an array safely handles paths with or without whitespace
MAPFILE_ARGS=()
if [[ "${BASH_VERSION%%.*}" -ge 4 ]]; then
    # Modern bash safety structure
    mapfile -t FORMAT_SRCS < <(find src include tests ../linux_driver/tests \
        \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) 2>/dev/null)
else
    # Fallback loop pattern for older shell environments
    while IFS= read -r line; do
        FORMAT_SRCS+=("$line")
    done < <(find src include tests ../linux_driver/tests \
        \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) 2>/dev/null)
fi

# 2. Check if any source files were discovered
if [ ${#FORMAT_SRCS[@]} -eq 0 ]; then
    echo "No C++ source files found matching target directories."
    exit 0
fi

# 3. Parse action commands
COMMAND="${1:-format}"

case "$COMMAND" in
    format)
        echo "Formatting C++ code with clang-format..."
        clang-format -i "${FORMAT_SRCS[@]}"
        echo "Formatting complete!"
        ;;
    check)
        echo "Checking C++ code formatting..."
        if clang-format --dry-run --Werror "${FORMAT_SRCS[@]}"; then
            echo "Format verification passed cleanly!"
        else
            echo "Format verification failed. Run './run-format.sh format' to fix."
            exit 1
        fi
        ;;
    *)
        echo "Usage: $0 {format|check}"
        exit 1
        ;;
esac

