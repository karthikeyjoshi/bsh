import os
import subprocess
import time
import shutil
import random
import string
import shlex
import json
import matplotlib.pyplot as plt

# --- 1. Auto-Detect Paths ---
# Gets the directory where benchmark.py is located (.../bsh/benchmark)
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
# Goes up one level to find the root (.../bsh)
REPO_ROOT = os.path.dirname(SCRIPT_DIR)

# Dynamically build the absolute paths
BSH_BIN_PATH = os.path.join(REPO_ROOT, "build", "bsh")
DAEMON_BIN_PATH = os.path.join(REPO_ROOT, "build", "bsh-daemon")
IMPORT_SCRIPT = os.path.join(REPO_ROOT, "import_zsh.py")
TEMP_DIR = os.path.join(SCRIPT_DIR, "bench_env_full")

# --- 2. Configuration ---
SIZES = [10_000, 50_000, 100_000, 250_000, 500_000] 
QUERY = "git commit" 
REPEATS = 5
OUTPUT_IMAGE = os.path.join(SCRIPT_DIR, "benchmark_realistic_all.png")

def random_hash(length=16):
    """Generates a random alphanumeric token to bloat the FTS dictionary."""
    return ''.join(random.choices(string.ascii_lowercase + string.digits, k=length))

def generate_command():
    # We still use realistic base structures, but inject massive entropy
    category = random.choices(
        ['git', 'web', 'filesystem', 'docker', 'junk'],
        weights=[25, 20, 20, 20, 15]
    )[0]

    if category == 'git':
        return random.choice([
            f"git commit -m 'fix issue in {random_hash(8)} regarding {random_hash(6)}'",
            f"git push origin feature/REQ-{random.randint(1000,9999)}-{random_hash(6)}",
            f"git clone git@github.com:{random_hash(8)}/{random_hash(12)}.git"
        ])
    elif category == 'web':
        # URLs, Bearer tokens, and IPs create massive FTS token bloat
        return random.choice([
            f"curl -H 'Authorization: Bearer {random_hash(32)}' https://api.{random_hash(10)}.com/v1/{random_hash(6)}",
            f"ping {random.randint(1,255)}.{random.randint(1,255)}.{random.randint(1,255)}.{random.randint(1,255)}",
            f"wget https://storage.googleapis.com/{random_hash(16)}/data.tar.gz"
        ])
    elif category == 'docker':
        return random.choice([
            f"docker run -e API_KEY={random_hash(24)} -d {random_hash(8)}:latest",
            f"docker exec -it {random_hash(12)} /bin/sh"
        ])
    elif category == 'filesystem':
        return random.choice([
            f"cat /var/log/{random_hash(8)}.log | grep '{random_hash(4)}'",
            f"echo '{random_hash(64)}' | base64 --decode",
            f"tar -czvf backup_{random_hash(8)}.tar.gz /workspace/{random_hash(6)}"
        ])
    elif category == 'junk':
        # Pure typing chaos (typos, accidental pastes)
        return random_hash(random.randint(10, 50))

def setup_isolation():
    if os.path.exists(TEMP_DIR):
        shutil.rmtree(TEMP_DIR)
    os.makedirs(TEMP_DIR)
    os.makedirs(os.path.join(TEMP_DIR, ".local", "share", "bsh"))
    os.makedirs(os.path.join(TEMP_DIR, ".config", "atuin"))
    os.makedirs(os.path.join(TEMP_DIR, ".local", "share", "atuin"))

def generate_history(n_lines):
    history_file = os.path.join(TEMP_DIR, ".zsh_history")
    print(f"[-] Generating {n_lines} highly variable commands...")
    
    base_timestamp = int(time.time()) - (86400 * 30)
    with open(history_file, "w") as f:
        for i in range(n_lines):
            cmd = generate_command()
            ts = base_timestamp + (i * random.randint(1, 60))
            duration = random.choice([0, 0, 1, 5, 12])
            f.write(f": {ts}:{duration};{cmd}\n")
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
    cmd_str = shlex.join(cmd_list) if isinstance(cmd_list, list) else cmd_list
    json_file = os.path.join(TEMP_DIR, "bench_output.json")
    if os.path.exists(json_file): os.remove(json_file)

    hyperfine_cmd = [
        "hyperfine", "--warmup", "2", "--min-runs", str(repeats),
        "--export-json", json_file, "--style", "none", "--ignore-failure",
        cmd_str
    ]

    result = subprocess.run(hyperfine_cmd, env=env, capture_output=True, text=True)
    if result.returncode != 0:
        return 0.0

    try:
        with open(json_file, 'r') as f:
            return json.load(f)["results"][0]["mean"] * 1000
    except Exception:
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
        print(" -> [BSH] Setup & Import...")
        subprocess.run(["python3", IMPORT_SCRIPT], env=env, stdout=subprocess.DEVNULL)
        daemon = subprocess.Popen([DAEMON_BIN_PATH], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1) 
        
        try:
            print(" -> [BSH] Measuring...")
            avg = measure_latency([BSH_BIN_PATH, "suggest", QUERY, "global", "", "0"], env)
            results["bsh"].append(avg)
            print(f"    BSH: {avg:.2f} ms")
        finally:
            daemon.terminate()
            daemon.wait()

        # 2. Atuin
        if has_atuin:
            print(" -> [Atuin] Setup & Import...")
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
    
    plt.plot(SIZES, results["bsh"], marker='o', color='red', label='BSH (Daemon + FTS5)', linewidth=2.5, zorder=10)
    plt.plot(SIZES, results["grep"], marker='x', color='black', label='Grep (Disk I/O)', linestyle='--', alpha=0.7)
    
    if has_atuin:
        plt.plot(SIZES, results["atuin"], marker='s', color='blue', label='Atuin (Rust/Cold Start)', linestyle='-')
    if has_fzf:
        plt.plot(SIZES, results["fzf"], marker='^', color='green', label='FZF (Pipe Overhead)', linestyle='-.')
    
    plt.title(f"Shell History Search Latency (Query: '{QUERY}')\nHigh-Cardinality Realistic Data")
    plt.xlabel("Database Size (Total Executions)")
    plt.ylabel("Latency (ms)")
    plt.grid(True, which="both", alpha=0.3)
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