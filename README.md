# BSH (Better Shell History)

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg) ![Language: C++20](https://img.shields.io/badge/Language-C%2B%2B20-orange) ![Platform: Linux%20%7C%20macOS](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-lightgrey)

**A High-Performance, Git-Aware, Predictive Terminal History Tool**

## 1. Executive Summary
BSH (Better Shell History) is a command-line interface (CLI) middleware designed to modernize the terminal experience by replacing standard flat history files (e.g., `.bash_history`) with a structured, local SQLite database.

Unlike traditional shells, BSH introduces a **"Live Predictive" TUI** (Text User Interface) that mimics IDE IntelliSense. It offers context-aware suggestions based on the current directory and Git branch as the user types, improving retrieval speed and reducing cognitive load.

---

## 2. Problem Statement
Standard shell history mechanisms (Bash/Zsh) suffer from three critical limitations in modern development workflows:

1.  **Context Blindness:** History is environment-agnostic. Commands executed within specific Git feature branches are intermixed with global commands, complicating the retrieval of branch-specific operations.
2.  **Reactive Searching:** Users are forced to actively initiate searches (e.g., `Ctrl+R`). There is no passive discovery or auto-completion based on historical frequency.
3.  **Pollution:** Failed commands (typos or non-zero exit codes) clutter search results, reducing efficiency.

---
## 3. Usage & Controls

The BSH TUI provides the following interaction modes:

| Key Binding | Action |
| :--- | :--- |
| **`Enter`** | Executes the user typed command. |
| **`Alt`/`Option` + `1-5`** | Instantly executes the corresponding suggestion from the Top 10 list. |
| **`Alt`/`Option` + `Arrows`** | Cycles search context: **Global** $\leftrightarrow$ **Directory** $\leftrightarrow$ **Git Branch**. |
| **`Ctrl` + `F`** | **Toggle Success Filter**: Show/hide failed commands (Exit code ≠ 0). |

---

## 4. Technical Architecture

### 4.1 Technology Stack
BSH is engineered for minimal latency (<5ms) using Modern C++ to ensure compliance with performance-critical CLI environments.

| Component | Library/Tool | Justification |
| :--- | :--- | :--- |
| **Core Language** | C++17 / C++20 | High performance and low latency. |
| **Database** | SQLite3 | Serverless, reliable, and supports concurrency. |
| **DB Wrapper** | SQLiteCpp | RAII wrapper for memory safety. |
| **TUI Renderer** | FTXUI | Rendering the "Live Prediction" overlay. |
| **Context Utils** | libgit2 | Efficient resolution of Git branch context. |

### 4.2 Data Model
BSH utilizes a relational schema to optimize storage and query performance.

* **`commands` Table:** Stores unique command strings to prevent redundancy.
* **`executions` Table:** Tracks the execution timeline, including:
    * **Session ID & Timestamp**
    * **CWD (Current Working Directory)**
    * **Git Branch:** Commands are tagged with the active branch.
    * **Exit Code:** Enables filtering of failed commands.
    * **Duration:** Execution time in milliseconds.

---

## 5. Key Features

### Live Predictive TUI
Integrated via the Zsh Line Editor (ZLE), BSH intercepts keystrokes to display a "Top 5" relevance list below the cursor in real-time.

### Deep Git Integration
BSH treats Git branches as distinct contexts. Users can filter history specifically by the current branch, solving the "Context Blindness" issue inherent in flat files.

### Success Filtering
A toggleable filter excludes commands with non-zero exit codes (`Exit Code ≠ 0`), ensuring that search results contain only valid, successful operations.

### Zero-Cloud Privacy
BSH operates with 100% local execution using C++ and SQLite. No data is transmitted to external servers, ensuring compliance with strict enterprise security policies.

---

## 6. Build Instructions

### Prerequisites and Dependencies

To build BSH, you need a standard C++ development environment and the core system libraries that `libgit2` and `SQLiteCpp` link against.

#### Core Build Tools (Required)

| Tool | Minimum Version | Description |
| :--- | :--- | :--- |
| **C++ Compiler** | GCC 9+ or Clang 10+ | Required for compiling the C++ source code. |
| **CMake** | 3.15+ | Required for configuring the build system and managing dependencies (FetchContent). |
| **Zsh** | N/A | Required for the interactive shell integration (`bsh_init.zsh` script). |

#### System Libraries (Required for Linking)

| Library | Purpose |
| :--- | :--- |
| **`libsqlite3`** | Local database management for history. |
| **`zlib`** | Compression/decompression for Git objects (used by libgit2). |
| **`libssl`/`libcrypto`** | Secure hashing and HTTPS connectivity (used by libgit2). |

---

#### Installation via Package Managers

Use your system's package manager to install the required development libraries before running the `./install.sh` script.

#### macOS (using Homebrew)

```bash
brew install cmake libgit2 sqlite zlib openssl
```


#### Fedora/CentOS/RHEL (using DNF)

```Bash

sudo dnf install gcc-c++ make cmake libgit2-devel sqlite-devel zlib-devel openssl-devel
```

### Installation

Once prerequisites are installed, proceed to the installation section and run the ./install.sh script.

```bash
# Clone the repository
git clone https://github.com/joshikarthikey/bsh.git
cd bsh

# Grant execute permission and run the installer
chmod +x install.sh
./install.sh 
```
## 7. Troubleshooting: Removing BSH and Preventing Shell Spam

If you choose to remove the BSH project, simply deleting the repository folder (e.g., `/Users/karthikey/bsh`) will cause your shell to spam error messages (`no such file or directory`) every time you open a new terminal session or execute a command.

This critical issue occurs because the integration lines in your Zsh startup files (`~/.zshrc`) and persistent Zsh hook definitions still attempt to load files that no longer exist.

To fully uninstall BSH and stop these errors, you must perform a manual cleanup of your shell configuration:

### 7.1 Clean up `~/.zshrc`

The first step is to remove the source command that loads the integration script at startup.

1.  Open your Zsh configuration file in a text editor:

    ```bash
    nano ~/.zshrc
    ```

2.  Locate and **delete the entire BSH integration block**, which typically looks similar to this:

    ```bash
    # BSH History Integration (Added by install.sh)
    source /path/to/your/bsh-repo/scripts/bsh_init.zsh 
    ```

### 7.2 Clean up Zsh Hooks (If Errors Persist)

If the errors persist after modifying `~/.zshrc`, the Zsh hook functions (like the command logger) are still defined in memory from the last successful source.

1.  **Restart** your terminal first. If the problem continues, run the following commands once in a clean session to explicitly remove the pre-command hook and any associated widgets:

    ```bash
    # Remove the function from the pre-command hook execution array
    precmd_functions=( ${precmd_functions[(I)_bsh_precmd]} )

    # Unload any associated widget functions (like the Ctrl+F toggle)
    unfunction _bsh_toggle_success_filter
    ```

### 7.3 Apply Changes

After cleaning up `~/.zshrc` and running the hook removal commands, **start a new terminal session** to confirm the errors have been completely eliminated.