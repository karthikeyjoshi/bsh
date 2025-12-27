#!/usr/bin/env zsh

# --- Configuration ---
BINARY_NAME="bsh"
DAEMON_NAME="bsh-daemon"
INSTALL_DIR="$HOME/.bsh"
BIN_PATH="$INSTALL_DIR/bin"
ZSH_INIT_FILE="scripts/bsh_init.zsh"
ZSHRC_PATH="$HOME/.zshrc"

# --- 1. Preparation and Cleanup ---
echo "Preparing BSH installation..."
mkdir -p "$BIN_PATH"
mkdir -p "$INSTALL_DIR/scripts"

# --- 2. Build the C++ Binary ---
echo "Building C++ binary..."
if ! command -v cmake &> /dev/null; then
    echo "Error: CMake is required but not found. Please install CMake."
    exit 1
fi

# Clean build directory and rebuild
rm -rf build
cmake -B build -G Ninja
cmake --build build --target "$BINARY_NAME"
# FIX: Build the daemon target explicitly as well
cmake --build build --target "$DAEMON_NAME"

if [[ $? -ne 0 ]]; then
    echo "Error: C++ compilation failed. Aborting."
    exit 1
fi

# --- 3. Install Components ---
echo "Installing binaries and scripts..."

# FIX: Copy BOTH binaries
cp "build/$BINARY_NAME" "$BIN_PATH/$BINARY_NAME"
cp "build/$DAEMON_NAME" "$BIN_PATH/$DAEMON_NAME"

# Copy the Zsh hook script
cp "$ZSH_INIT_FILE" "$INSTALL_DIR/scripts/"

# --- 4. Update Zsh Configuration ---
echo "Updating $ZSHRC_PATH..."

INIT_LINE="source $INSTALL_DIR/scripts/bsh_init.zsh"

if ! grep -q "$INIT_LINE" "$ZSHRC_PATH"; then
    echo "\n# BSH History Integration (Added by install.sh)" >> "$ZSHRC_PATH"
    echo "$INIT_LINE" >> "$ZSHRC_PATH"
    echo "Success! Please run 'source $ZSHRC_PATH' or restart your terminal."
else
    echo "Note: BSH initialization already found in $ZSHRC_PATH. Skipping update."
fi