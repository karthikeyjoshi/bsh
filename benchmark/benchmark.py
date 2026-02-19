import os
import subprocess
import time
import shutil
import random
import string
import shlex
import json
import matplotlib.pyplot as plt

# --- Configuration ---
SIZES = [10_000, 50_000, 100_000, 500_000] 
# FIX: Use a query that actually exists in the generated data
QUERY = "git status"
REPEATS = 5
OUTPUT_IMAGE = "benchmark_safe_all.png"
TEMP_DIR = os.path.abspath("bench_env_full")

# Paths (Adjust if necessary)
BSH_BIN_PATH = os.path.abspath("./build/bsh")
DAEMON_BIN_PATH = os.path.abspath("./build/bsh-daemon")
IMPORT_SCRIPT = os.path.abspath("import_zsh.py")

def setup_isolation():
    if os.path.exists(TEMP_DIR):
        shutil.rmtree(TEMP_DIR)
    os.makedirs(TEMP_DIR)
    os.makedirs(os.path.join(TEMP_DIR, ".local", "share", "bsh"))
    os.makedirs(os.path.join(TEMP_DIR, ".config", "atuin"))
    os.makedirs(os.path.join(TEMP_DIR, ".local", "share", "atuin"))

def generate_history(n_lines):
    history_file = os.path.join(TEMP_DIR, ".zsh_history")
    print(f"[-] Generating {n_lines} commands...")
    
    # FIX: Ensure our QUERY is present in the data
    common_cmds = ["git status", "ls -la", "cd ..", "docker ps", "cargo build", "npm start"]
    with open(history_file, "w") as f:
        for i in range(n_lines):
            if random.random() < 0.3:
                cmd = random.choice(common_cmds)
            else:
                cmd = f"echo 'random_{''.join(random.choices(string.ascii_lowercase, k=10))}'"
            timestamp = 1670000000 + i
            f.write(f": {timestamp}:0;{cmd}\n")
    return history_file

def get_isolated_env(hist_file):
    env = os.environ.copy()
    env["HOME"] = TEMP_DIR
    env["XDG_RUNTIME_DIR"] = TEMP_DIR 
    env["HISTFILE"] = hist_file
    env["ATUIN_CONFIG_DIR"] = os.path.join(TEMP_DIR, ".config", "atuin")
    env["ATUIN_DB_PATH"] = os.path.join(TEMP_DIR, ".local", "share", "atuin", "history.db")
    return env

def measure_latency(cmd_list, env, shell=False, repeats=REPEATS):
    # FIX: Use shlex.join to properly quote arguments (e.g., "git status")
    if isinstance(cmd_list, list):
        cmd_str = shlex.join(cmd_list)
    else:
        cmd_str = cmd_list

    json_file = os.path.join(TEMP_DIR, "bench_output.json")
    if os.path.exists(json_file): os.remove(json_file)

    hyperfine_cmd = [
        "hyperfine", 
        "--warmup", "2", 
        "--min-runs", str(repeats),
        "--export-json", json_file,
        "--style", "none",
        "--ignore-failure", # FIX: Don't crash if grep finds nothing (Exit 1)
        cmd_str
    ]

    result = subprocess.run(hyperfine_cmd, env=env, capture_output=True, text=True)

    if result.returncode != 0:
        print(f"\n[!] Hyperfine Failed: {result.stderr.strip()}")
        return 0.0

    try:
        with open(json_file, 'r') as f:
            data = json.load(f)
            return data["results"][0]["mean"] * 1000
    except Exception as e:
        return 0.0

def run_benchmark():
    results = {"bsh": [], "atuin": [], "fzf": [], "grep": []}
    has_atuin = shutil.which("atuin") is not None
    has_fzf = shutil.which("fzf") is not None
    
    for size in SIZES:
        print(f"\n=== Benchmarking Size: {size} ===")
        setup_isolation()
        hist_file = generate_history(size)
        env = get_isolated_env(hist_file)

        # 1. BSH
        print(" -> [BSH] Setup...")
        subprocess.run(["python3", IMPORT_SCRIPT], env=env, stdout=subprocess.DEVNULL)
        daemon = subprocess.Popen([DAEMON_BIN_PATH], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1) 
        
        try:
            print(" -> [BSH] Measuring...")
            # Note: We pass raw string for BSH args to avoid double quoting issues if any
            avg = measure_latency([BSH_BIN_PATH, "suggest", QUERY, "global", "", "0"], env)
            results["bsh"].append(avg)
            print(f"    BSH: {avg:.2f} ms")
        finally:
            daemon.terminate()
            daemon.wait()

        # 2. Atuin
        if has_atuin:
            print(" -> [Atuin] Setup...")
            subprocess.run(["atuin", "init", "zsh", "--disable-up-arrow"], env=env, stdout=subprocess.DEVNULL)
            with open(os.path.join(env["ATUIN_CONFIG_DIR"], "config.toml"), "w") as f:
                f.write("auto_sync = false\nupdate_check = false\n")
            subprocess.run(["atuin", "import", "zsh"], env=env, stdout=subprocess.DEVNULL)
            
            print(" -> [Atuin] Measuring...")
            avg = measure_latency(["atuin", "search", "--limit", "5", "--search-mode", "prefix", QUERY], env)
            results["atuin"].append(avg)
            print(f"    Atuin: {avg:.2f} ms")

        # 3. FZF
        if has_fzf:
            print(" -> [FZF] Measuring...")
            # FZF needs shell=True behavior, so we pass full string
            cmd = f"cat {hist_file} | fzf --filter='{QUERY}'"
            avg = measure_latency(cmd, env, shell=True)
            results["fzf"].append(avg)
            print(f"    FZF: {avg:.2f} ms")

        # 4. Grep
        print(" -> [Grep] Measuring...")
        avg = measure_latency(["grep", QUERY, hist_file], env)
        results["grep"].append(avg)
        print(f"    Grep: {avg:.2f} ms")

    return results, has_atuin, has_fzf

def plot_results(results, has_atuin, has_fzf):
    print(f"\nGenerating graph: {OUTPUT_IMAGE}")
    plt.figure(figsize=(10, 6))
    
    plt.plot(SIZES, results["bsh"], marker='o', color='red', label='BSH (Daemon)', linewidth=2.5, zorder=10)
    plt.plot(SIZES, results["grep"], marker='x', color='black', label='Grep (Disk I/O)', linestyle='--', alpha=0.7)
    
    if has_atuin:
        plt.plot(SIZES, results["atuin"], marker='s', color='blue', label='Atuin', linestyle='-')
    if has_fzf:
        plt.plot(SIZES, results["fzf"], marker='^', color='green', label='FZF', linestyle='-.')
    
    plt.title(f"Shell History Search Latency (Query: '{QUERY}')")
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
        if os.path.exists(TEMP_DIR):
            shutil.rmtree(TEMP_DIR)