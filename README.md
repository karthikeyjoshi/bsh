# BSH (Better Shell History)

**A High-Performance, Git-Aware, Predictive Terminal History Middleware**

## 1. Executive Summary

BSH (Better Shell History) is a robust CLI middleware designed to modernize the terminal experience by replacing standard flat history files with a structured, local SQLite database.

Engineered for performance-critical environments using C++20, BSH introduces a "Live Predictive" interface. It functions as an IntelliSense layer for the shell, intercepting keystrokes to provide context-aware suggestions based on the current working directory, active Git branch, and historical success rates.

## 2. Core Capabilities

### Context-Aware Retrieval

Standard shells treat history as a global list. BSH introduces dimensionality to command history:
* **Global Scope:** Search the entire execution history.
* **Directory Scope:** Filter commands executed specifically in the current folder.
* **Git Branch Scope:** Filter commands executed while on the active Git branch.

### Live Predictive Interface
Integrated directly via the Zsh Line Editor (ZLE), BSH renders a "Top 5" relevance list in real-time as the user types.

### Prompt Cycling (Opt-In)
Users can bind keys (such as Up/Down arrows) to cycle through BSH predictions directly in the command prompt, replacing the default history behavior with context-aware results.

### Exit Code Filtering
BSH tracks the exit code of every command. Users can toggle a "Success Filter" to instantly hide failed commands (typos, compilation errors).

### Local-First Architecture
BSH operates with a client-daemon architecture completely on the local machine. No telemetry or history data is transmitted to external servers.

## 3. Demo

![BSH Demo](assets/demo.gif)

## 4. Installation

BSH requires a C++20 compliant compiler (GCC 10+ or Clang 10+), CMake, and Ninja.

### 4.1 Install Dependencies

Ensure you have the development headers for `libgit2`, `sqlite3`, and `openssl`.

#### Ubuntu / Debian / Pop!_OS / Linux Mint
```bash
sudo apt update
sudo apt install build-essential cmake ninja-build libgit2-dev libsqlite3-dev libssl-dev zlib1g-dev pkg-config python3
```

#### Fedora / RHEL / CentOS
```bash
sudo dnf install gcc-c++ cmake ninja-build libgit2-devel sqlite-devel openssl-devel zlib-devel pkgconf python3
```

#### macOS (via Homebrew)
```bash
brew install cmake ninja libgit2 sqlite openssl python pkg-config
```

### 4.2 Build and Install

The provided install script compiles the binaries (Client and Daemon), sets up the directory structure in `~/.bsh`, and configures Zsh hooks.

```bash
# 1. Clone the repository
git clone https://github.com/joshikarthikey/bsh.git
cd bsh

# 2. Execute the installer
chmod +x install.sh
./install.sh
```

**Post-Installation:** Restart your terminal session or run `source ~/.zshrc` to initialize the integration.

### 4.3 Import Existing History

To migrate your existing `.zsh_history` into the BSH SQLite database:

```bash
python3 import_zsh.py
```

## 5. Usage & Key Bindings

The BSH interface activates automatically upon typing.

### Default Controls

| Key Binding | Action |
| :--- | :--- |
| **`Enter`** | Executes the user typed command. |
| **`Alt` + `1-5`** | Instantly executes the corresponding suggestion. |
| **`Alt` + `Shift` + `1-5`** | Pastes the suggestion into the prompt without executing. |
| **`Alt` + `Arrows`** | Cycles search context (Global / Directory / Branch). |
| **`Ctrl` + `F`** | **Toggle Success Filter**: Show/hide failed commands. |

### Enabling Arrow Key Cycling (Optional)

By default, BSH does not override your arrow keys. To enable cycling through BSH suggestions using `Up` and `Down` (similar to `zsh-history-substring-search`), add the following bindings to your `~/.zshrc` file **after** the BSH initialization line:

```zsh
# ~/.zshrc

# Option A: Standard Arrow Keys
bindkey '^[[A' _bsh_cycle_up
bindkey '^[[B' _bsh_cycle_down

# Option B: Vim Style (Ctrl+K / Ctrl+J)
bindkey '^K' _bsh_cycle_up
bindkey '^J' _bsh_cycle_down
```

## 6. Technical Architecture

BSH employs a high-performance Client-Daemon architecture to ensure zero latency on the main thread.

* **bsh-daemon:** A background C++ process managed by the shell script. It maintains the SQLite connection, handles libgit2 branch resolution, and performs asynchronous writes (WAL mode).
* **bsh (Client):** A lightweight ephemeral CLI tool. It communicates with the daemon via a Unix Domain Socket to dispatch search queries or log execution data.
* **Zsh Integration:** Leveraging zsh-hooks (`preexec`, `precmd`), BSH captures precise execution duration, timestamps, and exit codes without blocking the user's interactive session.

### Data Model

BSH utilizes a relational schema to optimize storage and query performance.

* **`commands` Table:** Stores unique command strings to prevent redundancy.
* **`executions` Table:** Tracks the execution timeline, including Session ID, CWD, Git Branch, Exit Code, and Duration.

## 7. Troubleshooting

### Daemon Management

The `bsh-daemon` is designed to auto-start. If suggestions disappear, the daemon may have been terminated.

* **Fix:** Type any character in the terminal. The `bsh_init.zsh` script attempts to respawn the daemon on input.
* **Manual Restart:** Run `pkill bsh-daemon`. The next keystroke will start a fresh instance.

### Uninstallation

To cleanly remove BSH:

1. **Remove Files:**
    ```bash
    rm -rf ~/.bsh
    ```

2. **Clean Configuration:** Open `~/.zshrc` and remove the integration block:
    ```bash
    # BSH History Integration (Added by install.sh)
    source /home/<user>/.bsh/scripts/bsh_init.zsh
    ```

3. **Reset Session:** Restart your terminal.
