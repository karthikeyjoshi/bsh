import sqlite3
import os
import re
import time

# Paths
HOME = os.path.expanduser("~")
ZSH_HISTORY = os.path.join(HOME, ".zsh_history")
BSH_DIR = os.path.join(HOME, ".local/share/bsh")
BSH_DB = os.path.join(BSH_DIR, "history.db")

def parse_zsh_history(file_path):
    pattern_extended = re.compile(r':\s*(\d+):(\d+);(.*)')
    print(f"Reading history from: {file_path}")
    
    with open(file_path, 'r', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line: continue

            match = pattern_extended.match(line)
            if match:
                timestamp = int(match.group(1))
                duration = int(match.group(2))
                cmd = match.group(3).strip() # FIX: TRIM HERE
                if cmd:
                    yield cmd, timestamp, duration
            else:
                cmd = line.strip() # FIX: TRIM HERE
                if cmd:
                    yield cmd, 0, 0

def import_history():
    # FIX: Remove old DB to clear duplicates/corruption
    if os.path.exists(BSH_DB):
        print("Removing old database to prevent duplicates...")
        os.remove(BSH_DB)
        # Also remove WAL/SHM files if they exist
        if os.path.exists(BSH_DB + "-wal"): os.remove(BSH_DB + "-wal")
        if os.path.exists(BSH_DB + "-shm"): os.remove(BSH_DB + "-shm")

    # Ensure directory exists
    if not os.path.exists(BSH_DIR):
        os.makedirs(BSH_DIR)

    print("Creating new database...")
    # Connect (This creates the file)
    conn = sqlite3.connect(BSH_DB)
    cursor = conn.cursor()
    
    # Enable WAL
    cursor.execute("PRAGMA journal_mode=WAL;")
    cursor.execute("PRAGMA synchronous=OFF;") 
    
    # Create Schema Manually (Since we deleted the file)
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS commands (
            id INTEGER PRIMARY KEY AUTOINCREMENT, 
            cmd_text TEXT UNIQUE NOT NULL
        );
    """)
    cursor.execute("""
        CREATE TABLE IF NOT EXISTS executions (
            id INTEGER PRIMARY KEY, 
            command_id INTEGER, 
            session_id TEXT, 
            cwd TEXT, 
            git_branch TEXT, 
            exit_code INTEGER, 
            duration_ms INTEGER, 
            timestamp INTEGER, 
            FOREIGN KEY (command_id) REFERENCES commands (id)
        );
    """)
    cursor.execute("CREATE INDEX IF NOT EXISTS idx_exec_cwd ON executions(cwd);")
    cursor.execute("CREATE INDEX IF NOT EXISTS idx_exec_branch ON executions(git_branch);")
    cursor.execute("CREATE INDEX IF NOT EXISTS idx_exec_ts ON executions(timestamp);")
    
    count = 0
    
    try:
        cursor.execute("BEGIN TRANSACTION;")
        
        for cmd, timestamp, duration in parse_zsh_history(ZSH_HISTORY):
            # 1. Insert Command (Ignore duplicates)
            cursor.execute("INSERT OR IGNORE INTO commands (cmd_text) VALUES (?)", (cmd,))
            
            # 2. Get the ID
            cursor.execute("SELECT id FROM commands WHERE cmd_text = ?", (cmd,))
            result = cursor.fetchone()
            
            if result:
                cmd_id = result[0]
                # 3. Insert Execution
                cursor.execute("""
                    INSERT INTO executions 
                    (command_id, session_id, cwd, git_branch, exit_code, duration_ms, timestamp)
                    VALUES (?, ?, ?, ?, ?, ?, ?)
                """, (cmd_id, "import", None, None, 0, duration, timestamp))
                count += 1
            
        conn.commit()
        print(f"Success! Imported {count} commands cleanly.")
        
    except Exception as e:
        print(f"Error during import: {e}")
        conn.rollback()
    finally:
        conn.close()

if __name__ == "__main__":
    if os.path.exists(ZSH_HISTORY):
        import_history()
    else:
        print(f"Could not find history file at {ZSH_HISTORY}")