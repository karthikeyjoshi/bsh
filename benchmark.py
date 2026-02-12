import os
import subprocess
import time
import shutil
import random
import string
import matplotlib.pyplot as plt

# --- Configuration ---
SIZES = [10_000, 50_000, 100_000, 500_000] # Number of commands
QUERY = "git commit"
REPEATS = 5
OUTPUT_IMAGE = "benchmark_safe_all.png"
TEMP_DIR = os.path.abspath("bench_env_full")

# Paths 
BSH_BIN_PATH = os.path.abspath("./build/bsh")
DAEMON_BIN_PATH = os.path.abspath("./build/bsh-daemon")
IMPORT_SCRIPT = os.path.abspath("import_zsh.py")

def setup_isolation():
    """Creates a fake $HOME environment to protect real user data."""
    if os.path.exists(TEMP_DIR):
        shutil.rmtree(TEMP_DIR)
    os.makedirs(TEMP_DIR)
    
    # Create required subdirs in the fake home
    os.makedirs(os.path.join(TEMP_DIR, ".local", "share", "bsh"))
    os.makedirs(os.path.join(TEMP_DIR, ".config", "atuin"))
    os.makedirs(os.path.join(TEMP_DIR, ".local", "share", "atuin"))

def generate_history(n_lines):
    """Generates a dummy .zsh_history inside the fake HOME."""
    history_file = os.path.join(TEMP_DIR, ".zsh_history")
    print(f"[-] Generating {n_lines} commands...")
    
    common_cmds = ["git status", "ls -la", "cd ..", "docker ps", "cargo build", "npm start"]
    with open(history_file, "w") as f:
        for i in range(n_lines):
            if random.random() < 0.3:
                cmd = random.choice(common_cmds)
            else:
                cmd = f"echo 'random_{''.join(random.choices(string.ascii_lowercase, k=10))}'"
            timestamp = 1670000000 + i
            # Zsh extended history format
            f.write(f": {timestamp}:0;{cmd}\n")
    return history_file

def get_isolated_env(hist_file):
    """Returns a modified environment dict pointing to the temp dir."""
    env = os.environ.copy()
    env["HOME"] = TEMP_DIR
    env["XDG_RUNTIME_DIR"] = TEMP_DIR # Crucial: Isolates the socket
    env["HISTFILE"] = hist_file       # For Atuin import
    
    # Atuin specific isolation
    env["ATUIN_CONFIG_DIR"] = os.path.join(TEMP_DIR, ".config", "atuin")
    env["ATUIN_DB_PATH"] = os.path.join(TEMP_DIR, ".local", "share", "atuin", "history.db")
    return env

def measure_latency(cmd_list, env, shell=False, repeats=REPEATS):
    latencies = []
    for _ in range(repeats):
        start = time.perf_counter()
        subprocess.run(
            cmd_list, 
            env=env, 
            shell=shell,
            stdout=subprocess.DEVNULL, 
            stderr=subprocess.DEVNULL
        )
        latencies.append((time.perf_counter() - start) * 1000)
    return sum(latencies) / len(latencies)

def run_benchmark():
    results = {"bsh": [], "atuin": [], "fzf": [], "grep": []}
    
    # Check for dependencies
    has_atuin = shutil.which("atuin") is not None
    has_fzf = shutil.which("fzf") is not None
    
    for size in SIZES:
        print(f"\n=== Benchmarking Size: {size} ===")
        setup_isolation()
        hist_file = generate_history(size)
        env = get_isolated_env(hist_file)

        # --- 1. BSH ---
        print(" -> [BSH] Importing...")
        subprocess.run(["python3", IMPORT_SCRIPT], env=env, stdout=subprocess.DEVNULL)
        
        print(" -> [BSH] Starting Daemon...")
        daemon = subprocess.Popen([DAEMON_BIN_PATH], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1) # Allow DB to initialize/vacuum
        
        try:
            print(" -> [BSH] Measuring...")
            avg = measure_latency([BSH_BIN_PATH, "suggest", QUERY, "global", "", "0"], env)
            results["bsh"].append(avg)
            print(f"    BSH: {avg:.2f} ms")
        finally:
            daemon.terminate()
            daemon.wait()

        # --- 2. Atuin ---
        if has_atuin:
            print(" -> [Atuin] Importing...")
            # We must disable sync to prevent it asking for login
            subprocess.run(["atuin", "init", "zsh", "--disable-up-arrow"], env=env, stdout=subprocess.DEVNULL)
            # Create a dummy config to ensure it doesn't try anything funny
            with open(os.path.join(env["ATUIN_CONFIG_DIR"], "config.toml"), "w") as f:
                f.write("auto_sync = false\nupdate_check = false\n")
            
            subprocess.run(["atuin", "import", "zsh"], env=env, stdout=subprocess.DEVNULL)
            
            print(" -> [Atuin] Measuring...")
            # Atuin search --limit 5 mimicry
            avg = measure_latency(["atuin", "search", "--limit", "5", "--search-mode", "prefix", QUERY], env)
            results["atuin"].append(avg)
            print(f"    Atuin: {avg:.2f} ms")
        else:
            print(" -> [Atuin] Not found, skipping.")
            results["atuin"].append(0)

        # --- 3. FZF ---
        if has_fzf:
            print(" -> [FZF] Measuring...")
            # Simulate: cat history | fzf --filter=query
            cmd = f"cat {hist_file} | fzf --filter='{QUERY}'"
            avg = measure_latency(cmd, env, shell=True)
            results["fzf"].append(avg)
            print(f"    FZF: {avg:.2f} ms")
        else:
            print(" -> [FZF] Not found, skipping.")
            results["fzf"].append(0)

        # --- 4. Grep ---
        print(" -> [Grep] Measuring...")
        avg = measure_latency(["grep", QUERY, hist_file], env)
        results["grep"].append(avg)
        print(f"    Grep: {avg:.2f} ms")

    return results, has_atuin, has_fzf

def plot_results(results, has_atuin, has_fzf):
    print(f"\nGenerating graph: {OUTPUT_IMAGE}")
    plt.figure(figsize=(10, 6))
    
    # BSH (Bold Red)
    plt.plot(SIZES, results["bsh"], marker='o', color='red', label='BSH (Daemon)', linewidth=2.5, zorder=10)
    
    # Grep (Dashed Black - Baseline)
    plt.plot(SIZES, results["grep"], marker='x', color='black', label='Grep (Disk I/O)', linestyle='--', alpha=0.7)
    
    # Competitors
    if has_atuin:
        plt.plot(SIZES, results["atuin"], marker='s', color='blue', label='Atuin', linestyle='-')
    
    if has_fzf:
        plt.plot(SIZES, results["fzf"], marker='^', color='green', label='FZF', linestyle='-.')
    
    plt.title(f"Shell History Search Latency (Isolated Environment)")
    plt.xlabel("Database Size (Commands)")
    plt.ylabel("Latency (ms)")
    plt.grid(True, which="both", alpha=0.2)
    plt.legend()
    plt.tight_layout()
    plt.savefig(OUTPUT_IMAGE)
    print("Done.")

if __name__ == "__main__":
    try:
        data, has_atuin, has_fzf = run_benchmark()
        plot_results(data, has_atuin, has_fzf)
    finally:
        # Cleanup
        if os.path.exists(TEMP_DIR):
            shutil.rmtree(TEMP_DIR)