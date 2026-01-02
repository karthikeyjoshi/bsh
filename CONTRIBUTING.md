# Contributing to BSH (Better Shell History)

Thank you for your interest in contributing to BSH. We aim to build a high-performance, context-aware shell history tool, and we welcome contributions from the community.

This document outlines the development environment, workflow, and guidelines for submitting pull requests.

## Development Environment

BSH is developed using **C++20**. To build and test the project locally, the following dependencies are required:

### Dependencies

* **C++ Compiler:** GCC 10+ or Clang 10+ (Must support C++20)
* **Build System:** `cmake` (3.15+) and `ninja-build`
* **Libraries:** `libgit2`, `sqlite3`, `openssl`, `zlib`, `python3`

### Setting up the Repository

1. **Fork** the repository on GitHub.
2. **Clone** your fork locally:

    ```bash
    git clone [https://github.com/YOUR_USERNAME/bsh.git](https://github.com/YOUR_USERNAME/bsh.git)
    cd bsh
    ```

3. **Build** using CMake:

    ```bash
    mkdir build && cd build
    cmake -G Ninja ..
    cmake --build .
    ```

---

## Development Workflow

BSH operates on a Client-Daemon architecture, which requires a specific workflow for testing and debugging.

### 1. Debugging the Daemon

The `bsh-daemon` typically runs in the background. To debug logic changes (database queries, git resolution, etc.), follow these steps:

1. **Terminate existing instances:**

    ```bash
    pkill bsh-daemon
    ```

2. **Run the daemon manually** in a separate terminal window. This allows direct observation of `std::cout` and `std::cerr` output:

    ```bash
    ./build/bsh-daemon
    ```

3. **Run the client** in your main terminal to send requests to the debug daemon:

    ```bash
    # Example: Search for "git" in global scope
    ./build/bsh suggest "git" global "" 0
    ```

### 2. Testing Zsh Integration

When modifying `scripts/bsh_init.zsh`:

1. Execute `source scripts/bsh_init.zsh` in the current shell to reload function definitions.
2. Type in the terminal to trigger the `_bsh_refresh_suggestions` hook.

### 3. Database Schema Changes

BSH uses **SQLite schema versioning** via `PRAGMA user_version` to manage database evolution safely.

All schema migrations are applied **at daemon startup**, before the daemon begins serving client requests.

#### Adding or Modifying Schema Versions

1. Update the target schema version in `HistoryDB::initSchema()`.
2. Add a new migration step in the version loop (e.g. `v1 → v2`, `v2 → v3`).
3. Each migration step **must** be executed inside a transaction.

#### Destructive Schema Changes (Example)

For schema changes that SQLite cannot perform using `ALTER TABLE`
(e.g. dropping columns or changing column types), use the standard
**create → copy → drop → rename** migration pattern.

Below is an example migration (`v1 → v2`) implemented using the
same C++/SQLiteCpp style as the codebase:

```cpp
else if (current_version == 1) {
    // v1 → v2 : example destructive migration

    // 1. Create new table with updated schema
    db_->exec(
        "CREATE TABLE executions_new ("
        "id INTEGER PRIMARY KEY, "
        "command_id INTEGER, "
        "session_id TEXT, "
        "cwd TEXT, "
        "git_branch TEXT, "
        "exit_code INTEGER, "
        "duration_ms INTEGER, "
        "timestamp INTEGER, "
        "new_column TEXT, "         // <-- new column
        "FOREIGN KEY (command_id) REFERENCES commands (id)"
        ");"
    );

    // 2. Copy data from old table
    // put either 'NULL' or a default value into new column for old data
    db_->exec("INSERT INTO executions_new ("
                "id, command_id, session_id, cwd, git_branch, exit_code, duration_ms, timestamp, new_column) "
                "SELECT "
                "id, command_id, session_id, cwd, git_branch, exit_code, duration_ms, timestamp, 'migrated_data' "      // mark empty values as text 'migrated_data'
                "FROM executions; "
            );

    // 3. Swap Tables: Drop the old one, rename the new one
    // eg. drop table executions and rename execution_new to execution
    db_->exec("DROP TABLE executions;");
    db_->exec("ALTER TABLE executions_new RENAME TO executions;");

    // 4. Recreate indexes
    db_->exec("CREATE INDEX idx_exec_cwd ON executions(cwd);");
    db_->exec("CREATE INDEX idx_exec_branch ON executions(git_branch);");
    db_->exec("CREATE INDEX idx_exec_ts ON executions(timestamp);");

    // 5. Increment schema version
    current_version = 2;
    db_->exec("PRAGMA user_version = 2");

}
```


---

## Performance and Benchmarking

As BSH is a latency-critical application, performance is paramount. Any changes to the suggestion path (Client -> Socket -> Daemon -> DB -> Socket -> Client) must be optimized.

Before submitting a Pull Request that affects core logic, please run the benchmark suite:

```bash
# Requires 'hyperfine' installed
./benchmark.sh
```

Include the output of this benchmark in your Pull Request description for performance-related changes.

---

## Project Structure

* `src/daemon.cpp`: The background server. Handles SQLite connections, Git context, and request processing.
* `src/client.cpp`: Lightweight ephemeral binary. Sends arguments to the daemon via IPC.
* `src/db.cpp`: SQLite interactions. Optimization targets (indexes, query tuning) are located here.
* `src/tui.cpp`: FTXUI logic for the interactive selection menu.
* `scripts/`: Zsh hooks (ZLE integration) that connect the binary to the shell.

---

## Pull Request Guidelines

1. Topic Branches: Create a new branch for each feature or fix (e.g., `feat/fts5-search` or `fix/socket-permissions`).

2. Code Style:

    * Adhere to Modern C++ standards (e.g., `std::string_view`, smart pointers, `auto`).
    * Avoid manual memory management (`new`/`delete`) unless strictly necessary.
    * Ensure headers are included in `src/`.

3. Commit Messages: Please use Conventional Commits:

    * feat: add support for bash
    * fix: resolve crash on empty git branch
    * perf: implement LRU cache for git resolution

---

## Community

For questions regarding implementation or architecture, please open a Discussion or an Issue tagged with question.
