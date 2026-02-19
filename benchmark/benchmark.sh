# --- Configuration ---
BUILD_DIR="build"
BINARY="./$BUILD_DIR/bsh"
WARMUP=10
MIN_RUNS=50

# --- Colors ---
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== BSH Performance Benchmark Suite ===${NC}"

# 1. Dependency Check
if ! command -v hyperfine &> /dev/null; then
    echo -e "${RED}Error: 'hyperfine' is not installed.${NC}"
    echo "Please install it: brew install hyperfine / sudo apt install hyperfine"
    exit 1
fi

# 2. Build Latest Version
echo -e "\n${BLUE}[+] Building latest binaries...${NC}"
cmake -B "$BUILD_DIR" -G Ninja > /dev/null
cmake --build "$BUILD_DIR" --target bsh bsh-daemon > /dev/null

if [[ $? -ne 0 ]]; then
    echo -e "${RED}Build failed! Aborting tests.${NC}"
    exit 1
fi

# 3. Ensure Daemon is Running (Restart with new binary)
echo -e "${BLUE}[+] Restarting Daemon...${NC}"
pkill bsh-daemon
./$BUILD_DIR/bsh-daemon &> /dev/null &
# Give it a second to initialize DB and socket
sleep 1

# 4. Test 1: The Optimization (Daemon vs Forking Git)
echo -e "\n${GREEN}Test 1: Git Branch Resolution (Latency)${NC}"
echo "Comparing: New Daemon Architecture vs. Old Shell Forking method"

hyperfine --warmup "$WARMUP" --min-runs "$MIN_RUNS" \
  --export-markdown benchmark_git.md \
  -n "New BSH (Daemon + libgit2)" \
    "$BINARY suggest 'ls' branch '$(pwd)' 0" \
  -n "Old Way (Shell + git binary)" \
    "git rev-parse --abbrev-ref HEAD && $BINARY suggest 'ls' branch 'main' 0"

# 5. Test 2: External Competitor (Atuin)
echo -e "\n${GREEN}Test 2: Competitive Analysis (BSH vs Atuin)${NC}"

if command -v atuin &> /dev/null; then
    hyperfine --warmup "$WARMUP" --min-runs "$MIN_RUNS" \
      --export-markdown benchmark_atuin.md \
      -n "BSH (C++ Daemon)" \
        "$BINARY suggest 'ls' global '' 0" \
      -n "Atuin (Rust CLI)" \
        "atuin search --search-mode prefix --limit 5 'ls' || true"
else
    echo -e "${BLUE}Skipping Test 2: 'atuin' not found in PATH.${NC}"
fi

# 6. Test 3: Typing Latency (Base Overhead)
echo -e "\n${GREEN}Test 3: Base Typing Latency (Global Mode)${NC}"
hyperfine --warmup "$WARMUP" --min-runs 100 \
  -n "Keystroke Latency" \
  "$BINARY suggest 'doc' global '' 0"

echo -e "\n${BLUE}=== Benchmarks Complete ===${NC}"
echo "Results saved to benchmark_git.md and benchmark_atuin.md"