#!/bin/bash

# --- 1. Auto-Detect Repository Root ---
# Get the directory where this script is actually located
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
# Assuming the script is in the root. If it's in scripts/, change to "$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$SCRIPT_DIR"

# Check if CMakeLists.txt is here. If not, maybe we are in a subdir?
if [ ! -f "$REPO_ROOT/CMakeLists.txt" ]; then
    echo "CMakeLists.txt not found in $REPO_ROOT. Assuming parent dir..."
    REPO_ROOT="$(dirname "$SCRIPT_DIR")"
fi

if [ ! -f "$REPO_ROOT/CMakeLists.txt" ]; then
    echo "Error: Could not locate repo root containing CMakeLists.txt"
    exit 1
fi

# Move to Repo Root for the duration of the script
cd "$REPO_ROOT" || exit 1

# --- Configuration ---
BUILD_DIR="build"
BINARY="./$BUILD_DIR/bsh"
DAEMON="./$BUILD_DIR/bsh-daemon"
HELLO_SRC="hello_bench.cpp"
HELLO_BIN="./$BUILD_DIR/hello_bench"
OUTPUT_FILE="benchmark/benchmark_results.md"
WARMUP=10
MIN_RUNS=50

# --- Colors ---
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}=== BSH Unified Performance Benchmark ===${NC}"
echo "Running from: $(pwd)"

# 2. Dependency Check
if ! command -v hyperfine &> /dev/null; then
    echo -e "${RED}Error: 'hyperfine' is not installed.${NC}"
    exit 1
fi

# 3. Build Tools & Baseline
echo -e "\n${BLUE}[+] Building binaries...${NC}"
# Use -S . to explicitly say "Source is here" and -B build
cmake -S . -B "$BUILD_DIR" -G Ninja > /dev/null
cmake --build "$BUILD_DIR" --target bsh bsh-daemon > /dev/null

# Create Baseline C++ Hello World
echo '#include <iostream>
int main() { std::cout << "Hello"; return 0; }' > "$HELLO_SRC"
g++ -O3 "$HELLO_SRC" -o "$HELLO_BIN"

if [[ $? -ne 0 ]]; then
    echo -e "${RED}Build failed! Aborting tests.${NC}"
    exit 1
fi

# 4. Restart Daemon
echo -e "${BLUE}[+] Restarting Daemon...${NC}"
pkill bsh-daemon
"$DAEMON" &> /dev/null &
DAEMON_PID=$!
sleep 1

echo -e "\n${GREEN}Starting Benchmark...${NC}"

# 5. Construct Command List (Fixed Flags)
# Note: Hyperfine uses '-n' for name, NOT '--n'
CMDS=(
    -n "Baseline (C++ Hello World)" "$HELLO_BIN"
    -n "BSH (Client -> Daemon)" "$BINARY suggest 'git' global '' 0"
)

if command -v atuin &> /dev/null; then
    CMDS+=(-n "Atuin (Rust CLI)" "atuin search --search-mode prefix --limit 5 'git' || true")
fi

# 6. Run Hyperfine
hyperfine --warmup "$WARMUP" --min-runs "$MIN_RUNS" \
  --export-markdown "$OUTPUT_FILE" \
  --style full \
  "${CMDS[@]}"

echo -e "\n${BLUE}=== Done ===${NC}"
echo "Results saved to $OUTPUT_FILE"

# Cleanup
rm "$HELLO_SRC"
# kill $DAEMON_PID # Uncomment if you want the daemon to stop after testing