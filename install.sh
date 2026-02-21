set -e

REPO="joshikarthikey/bsh"
INSTALL_DIR="$HOME/.bsh"
ZSHRC_PATH="$HOME/.zshrc"

VERSION="${BSH_VERSION:-latest}"

GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m'

echo -e "${BLUE}=== Installing BSH (Better Shell History) ===${NC}"

OS="$(uname -s)"
ARCH="$(uname -m)"

if [ "$OS" = "Linux" ] && [ "$ARCH" = "x86_64" ]; then
    TARGET="bsh-linux-x86_64"
elif [ "$OS" = "Darwin" ] && [ "$ARCH" = "arm64" ]; then
    TARGET="bsh-macos-arm64"
elif [ "$OS" = "Darwin" ] && [ "$ARCH" = "x86_64" ]; then
    TARGET="bsh-macos-x86_64"
else
    echo -e "${RED}Error: Unsupported OS or Architecture ($OS $ARCH)${NC}"
    echo "Currently, BSH provides pre-compiled binaries for Linux (x86_64) and macOS (x86_64, arm64)."
    exit 1
fi

echo -e "Detected platform: ${GREEN}$TARGET${NC}"

if [ "$VERSION" = "latest" ]; then
    DOWNLOAD_URL="https://github.com/$REPO/releases/latest/download/${TARGET}.tar.gz"
    echo "Downloading latest release from GitHub..."
else
    DOWNLOAD_URL="https://github.com/$REPO/releases/download/${VERSION}/${TARGET}.tar.gz"
    echo "Downloading release ${VERSION} from GitHub..."
fi

mkdir -p "$INSTALL_DIR"

if ! curl -sSL "$DOWNLOAD_URL" | tar -xz -C "$INSTALL_DIR"; then
    echo -e "${RED}Error: Failed to download or extract the release.${NC}"
    echo "URL attempted: $DOWNLOAD_URL"
    echo "Please check your internet connection or verify that the version exists."
    exit 1
fi

chmod +x "$INSTALL_DIR/bin/bsh-daemon"

pkill bsh-daemon || true

echo -e "Updating $ZSHRC_PATH..."

INIT_LINE="source $INSTALL_DIR/scripts/bsh_init.zsh"

if [ -f "$ZSHRC_PATH" ] && grep -q "$INIT_LINE" "$ZSHRC_PATH"; then
    echo "Note: BSH initialization is already present in your .zshrc."
else
    echo -e "\n# BSH History Integration" >> "$ZSHRC_PATH"
    echo "$INIT_LINE" >> "$ZSHRC_PATH"
    echo -e "${GREEN}Added BSH hook to your .zshrc!${NC}"
fi

echo -e "\n${BLUE}=== Installation Complete! ===${NC}"
echo -e "To start using BSH, either restart your terminal or run:"
echo -e "  ${GREEN}source ~/.zshrc${NC}\n"