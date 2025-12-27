# BSH (Better Shell History)

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg) ![Language: C++20](https://img.shields.io/badge/Language-C%2B%2B20-orange) ![Platform: Linux%20%7C%20macOS](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-lightgrey)

**A High-Performance, Git-Aware, Predictive Terminal History Middleware**

## 1. Executive Summary

BSH (Better Shell History) is a robust CLI middleware designed to modernize the terminal experience by replacing standard flat history files (e.g., `.zsh_history`) with a structured, local SQLite database.

Engineered for performance-critical environments using C++20, BSH introduces a "Live Predictive" TUI (Text User Interface). It functions as an IntelliSense layer for the shell, intercepting keystrokes to provide context-aware suggestions based on the current working directory, active Git branch, and historical success rates.

---

## 2. Core Capabilities

### Context-Aware Retrieval

Standard shells treat history as a global list, often leading to irrelevant results. BSH introduces dimensionality to command history:

* **Global Scope:** Search the entire execution history.
* **Directory Scope:** Filter commands executed specifically in the current folder.
* **Git Branch Scope:** Filter commands executed while on the active Git branch (e.g., retrieving specific build flags used in `feature/login` but not `main`).

### Live Predictive TUI

Integrated directly via the Zsh Line Editor (ZLE), the BSH interface renders a "Top 5" relevance list in real-time as the user types, eliminating the need for reactive searching (`Ctrl+R`).

### Exit Code Filtering

BSH tracks the exit code of every command. Users can toggle a "Success Filter" to instantly hide failed commands (typos, compilation errors), ensuring suggestions are valid, executable operations.

### Local-First Architecture

BSH operates with a client-daemon architecture completely on the local machine. No telemetry or history data is transmitted to external servers, ensuring compliance with strict data privacy policies.

---

## 3. Installation

BSH requires a C++20 compliant compiler, CMake, and Ninja.

### 3.1 Install Dependencies

#### Ubuntu / Debian / Pop!_OS / Linux Mint

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build libgit2-dev libsqlite3-dev libssl-dev zlib1g-dev python3
```

#### Fedora / RHEL / CentOS

```bash
sudo dnf install gcc-c++ make cmake ninja-build libgit2-devel sqlite-devel zlib-devel openssl-devel python3
```

#### macOS (via Homebrew)

```bash
brew install cmake ninja libgit2 sqlite zlib openssl python
```

### 3.2 Build and Install

The provided install.sh script compiles the binaries (Client and Daemon), sets up the directory structure in ~/.bsh, and configures Zsh hooks.

```bash
# 1. Clone the repository
git clone [https://github.com/joshikarthikey/bsh.git](https://github.com/joshikarthikey/bsh.git)
cd bsh

# 2. Execute the installer
chmod +x install.sh
./install.sh
```

**Post-Installation:** Restart your terminal session or run source ~/.zshrc to initialize the integration.

### 3.3 Import Existing History

To migrate your existing .zsh_history into the BSH SQLite database:

``` bash
python3 import_zsh.py
```

## Usage & Key Bindings

The BSH TUI activates automatically upon typing.

| Key Binding | Action |
| :--- | :--- |
| **`Enter`** | Executes the user typed command. |
| **`Alt`/`Option` + `1-5`** | Instantly executes the corresponding suggestion from the Top 10 list. |
| **`Alt`/`Option` + `Arrows`** | Cycles search context: **Global** $\leftrightarrow$ **Directory** $\leftrightarrow$ **Git Branch**. |
| **`Ctrl` + `F`** | **Toggle Success Filter**: Show/hide failed commands (Exit code ≠ 0). |

## 5. Technical Architecture

BSH employs a high-performance Client-Daemon architecture to ensure zero latency (<5ms) on the main thread.

* **bsh-daemon:** A background C++ process managed by the shell script. It maintains the SQLite connection, handles libgit2 branch resolution, and performs asynchronous writes (WAL mode).
* **bsh (Client):** A lightweight ephemeral CLI tool. It communicates with the daemon via a Unix Domain Socket (/tmp/bsh.sock) to dispatch search queries or log execution data.
* **Zsh Integration:** Leveraging zsh-hook (preexec, precmd), BSH captures precise execution duration, timestamps, and exit codes without blocking the user's interactive session.

### 5.1 Technology Stack

BSH is engineered for minimal latency (<5ms) using Modern C++ to ensure compliance with performance-critical CLI environments.

| Component | Library/Tool | Justification |
| :--- | :--- | :--- |
| **Core Language** | C++17 / C++20 | High performance and low latency. |
| **Database** | SQLite3 | Serverless, reliable, and supports concurrency. |
| **DB Wrapper** | SQLiteCpp | RAII wrapper for memory safety. |
| **TUI Renderer** | FTXUI | Rendering the "Live Prediction" overlay. |
| **Context Utils** | libgit2 | Efficient resolution of Git branch context. |

### 5.2 Data Model

BSH utilizes a relational schema to optimize storage and query performance.

* **`commands` Table:** Stores unique command strings to prevent redundancy.
* **`executions` Table:** Tracks the execution timeline, including:
  * **Session ID & Timestamp**
    * **CWD (Current Working Directory)**
    * **Git Branch:** Commands are tagged with the active branch.
    * **Exit Code:** Enables filtering of failed commands.
    * **Duration:** Execution time in milliseconds.

---

## 6. Troubleshooting & Maintenance

### Daemon Management

The `bsh-daemon` is designed to auto-start. If suggestions disappear, the daemon may have been terminated.

* **Fix:** Simply type any character in the terminal. The `bsh_init.zsh` script attempts to respawn the daemon on input.

* **Manual Restart:** `pkill bsh-daemon` (The next keystroke will start a fresh instance).

### Uninstallation

To cleanly remove BSH:

1. **Remove Files:**

```Bash
rm -rf ~/.bsh
```

2. **Clean Configuration:** Open `~/.zshrc` and remove the integration block:

```Bash
# BSH History Integration (Added by install.sh)
source /home/<user>/.bsh/scripts/bsh_init.zsh
```

3. Reset Session: Restart your terminal. If errors persist (e.g., `command not found: _bsh_precmd`), run:

```Bash
precmd_functions=( ${precmd_functions[(I)_bsh_precmd]} )
unfunction _bsh_toggle_success_filter
```

### 7.3 Apply Changes

After cleaning up `~/.zshrc` and running the hook removal commands, **start a new terminal session** to confirm the errors have been completely eliminated.