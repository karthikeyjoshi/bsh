# scripts/bsh_init.zsh

local BSH_INIT_SCRIPT_PATH="${(%):-%N}"
export BSH_REPO_ROOT="$(dirname $(dirname $BSH_INIT_SCRIPT_PATH))"

local BSH_INIT_SCRIPT_PATH="${(%):-%N}"
export BSH_REPO_ROOT="$(dirname $(dirname $BSH_INIT_SCRIPT_PATH))"

if [[ -x "$BSH_REPO_ROOT/bin/bsh" ]]; then
    # Production/Installed path (~/.bsh/bin/bsh)
    export BSH_BINARY="$BSH_REPO_ROOT/bin/bsh"
elif [[ -x "$BSH_REPO_ROOT/build/bsh" ]]; then
    # Development/Source path (./build/bsh)
    export BSH_BINARY="$BSH_REPO_ROOT/build/bsh"
else
    echo "BSH Error: Binary not found in $BSH_REPO_ROOT/bin or $BSH_REPO_ROOT/build"
    return 1
fi

# State Variables
typeset -gA _bsh_suggestions
typeset -g _bsh_start_time
typeset -g _bsh_current_cmd
typeset -g _bsh_mode=0 # 0=Global, 1=Directory, 2=Branch

# --- GIT HELPER ---
_bsh_get_branch() {
git rev-parse --abbrev-ref HEAD 2>/dev/null
}

# Initialize the variable (0 = show all, 1 = success only)
_bsh_filter_success=0

_bsh_toggle_success_filter() {
# Toggle between 0 and 1
if [[ $_bsh_filter_success -eq 0 ]]; then
_bsh_filter_success=1
else
_bsh_filter_success=0
fi
# Refresh suggestions with the new filter applied
_bsh_refresh_suggestions
zle redisplay
}

# Register and bind
zle -N _bsh_toggle_success_filter
bindkey '^F' _bsh_toggle_success_filter

# --- SUGGESTION ENGINE ---
_bsh_refresh_suggestions() {
# 1. Validation
if [[ ! -x "$BSH_BINARY" ]]; then return; fi
# Don't suggest if buffer is empty or just whitespace
if [[ -z "${BUFFER// }" ]]; then
POSTDISPLAY=""
return
fi

# 2. Prepare Arguments
local args=("$BUFFER" "--scope")
local header_text=" BSH: Global "
if [[ $_bsh_mode -eq 1 ]]; then
args+=("dir" "--cwd" "$PWD")
header_text=" BSH: Directory "
elif [[ $_bsh_mode -eq 2 ]]; then
local branch=$(_bsh_get_branch)
[[ -z "$branch" ]] && branch="unknown"
args+=("branch" "--branch" "$branch")
header_text=" BSH: Branch ($branch) "
else
args+=("global")
fi

# Add Success Filter if active
if [[ $_bsh_filter_success -eq 1 ]]; then
args+=("--success")
header_text="${header_text% } [OK] "
fi

# 3. Execution & Parsing
# We use 'cat' to ensure we don't trip over raw newlines in variables
local output
output=$("$BSH_BINARY" suggest "${args[@]}" 2>/dev/null)
if [[ -z "$output" ]]; then
POSTDISPLAY=""
_bsh_suggestions=()
return
fi

# 4. Build Lines & Calculate Width
_bsh_suggestions=()
local -a display_lines
local max_len=${#header_text}
local i=0
# Determine Terminal Width constraints
# Fallback to 80 if COLUMNS is unset
local term_width=${COLUMNS:-80}
# MATH FIX:
# Width = (Text Length) + 4 (Padding) + 2 (Borders │...│)
# Total Width <= COLUMNS
# Text Length <= COLUMNS - 6
# We use -7 for an extra safety buffer.
local safe_text_limit=$((term_width - 7))

# Read output line by line
while IFS= read -r line; do
# Ignore empty lines
[[ -z "${line// }" ]] && continue
# Stop after 5 suggestions
[[ $i -ge 5 ]] && break

_bsh_suggestions[$i]="$line"
# Format: "1: command "
local display_num=$((i + 1))
local text=" $display_num: $line"
# --- TRUNCATION LOGIC ---
# If the text is wider than the safe limit, truncate and add "..."
if (( ${#text} > safe_text_limit )); then
# Truncate to limit - 3 to make room for "..."
text="${text:0:$((safe_text_limit - 3))}..."
fi

# Calculate visual length (strip ANSI codes if any exist)
local clean_text=${text//$'\e'[\[(]*([0-9;])[@-~]/}
local text_len=${#clean_text}

if (( text_len > max_len )); then max_len=$text_len; fi
display_lines+=("$text")
((i++))
done <<< "$output"

# Add the visual padding (this is where the extra width comes from)
max_len=$((max_len + 4))

# If no valid lines found after parsing
if [[ ${#display_lines[@]} -eq 0 ]]; then
POSTDISPLAY=""
return
fi

# 5. Draw Box (Strict Padding)
local result=$'\n'
# Top Border
local top_content="╭$header_text"
# Pad with '─' to max_len+1
result+="${(r:max_len+1::─:)top_content}╮"

# Content Lines
for line in "${display_lines[@]}"; do
result+=$'\n'
# Pad with ' ' to max_len
result+="│${(r:max_len:: :)line}│"
done
# Bottom Border
result+=$'\n'
local bot_content="╰"
# Pad with '─' to max_len+1
result+="${(r:max_len+1::─:)bot_content}╯"

POSTDISPLAY="$result"
}

# --- STATE SWITCHER ---

# Helper to check if we are in a git repo
_bsh_is_git() {
    git rev-parse --is-inside-work-tree &>/dev/null
}

_bsh_cycle_mode_fwd() { 
    # 1. Increment Mode
    (( _bsh_mode = (_bsh_mode + 1) % 3 ))
    
    # 2. If we landed on Branch Mode (2) but aren't in Git, skip to Global (0)
    if [[ $_bsh_mode -eq 2 ]] && ! _bsh_is_git; then
        _bsh_mode=0
    fi
    
    _bsh_refresh_suggestions
    zle -R 
}

_bsh_cycle_mode_back() { 
    # 1. Decrement Mode
    (( _bsh_mode = _bsh_mode - 1 ))
    
    # 2. Handle Wrap Around (Global 0 -> Branch 2)
    if (( _bsh_mode < 0 )); then 
        _bsh_mode=2 
    fi
    
    # 3. If we landed on Branch Mode (2) but aren't in Git, skip to Directory (1)
    if [[ $_bsh_mode -eq 2 ]] && ! _bsh_is_git; then
        _bsh_mode=1
    fi

    _bsh_refresh_suggestions
    zle -R 
}

zle -N _bsh_cycle_mode_fwd
zle -N _bsh_cycle_mode_back
bindkey "^[f" _bsh_cycle_mode_fwd
bindkey "^[[1;3C" _bsh_cycle_mode_fwd
bindkey "^[b" _bsh_cycle_mode_back
bindkey "^[[1;3D" _bsh_cycle_mode_back

# --- RECORDING HOOKS ---
_bsh_preexec() {
_bsh_current_cmd="$1"
zmodload zsh/datetime
_bsh_start_time=$EPOCHREALTIME
}
_bsh_precmd() {
local exit_code=$?
if [[ -z "$_bsh_start_time" || -z "$_bsh_current_cmd" ]]; then return; fi
local now=$EPOCHREALTIME; local duration=$(( (now - _bsh_start_time) * 1000 ))
local cmd_log="$_bsh_current_cmd"
_bsh_start_time=""; _bsh_current_cmd=""
"$BSH_BINARY" record --cmd "$cmd_log" --cwd "$PWD" --exit "$exit_code" --duration "${duration%.*}" --session "$$"
}
autoload -Uz add-zsh-hook
add-zsh-hook preexec _bsh_preexec
add-zsh-hook precmd _bsh_precmd

# --- TYPING HOOKS ---
_bsh_self_insert() { zle .self-insert; _bsh_refresh_suggestions; }
zle -N self-insert _bsh_self_insert
_bsh_backward_delete_char() { zle .backward-delete-char; _bsh_refresh_suggestions; }
zle -N backward-delete-char _bsh_backward_delete_char

# --- EXECUTION BINDINGS (FIXED) ---

# Standard Enter Key
_bsh_accept_line() {
# DO NOT overwrite BUFFER with the top suggestion.
# Just clear the UI and run what the user actually typed.
# 1. Clear the box
POSTDISPLAY=""
# 2. Force Redraw (Wipes ghost text)
zle -R
# 3. Execute
zle .accept-line
}
zle -N accept-line _bsh_accept_line



# Shortcut Keys (1-5)
_bsh_run_idx() {
local display_num=$1
local idx=$((display_num - 1))
if [[ -n "${_bsh_suggestions[$idx]}" ]]; then
BUFFER="${_bsh_suggestions[$idx]}"
# 1. Clear the box
POSTDISPLAY=""
# 2. Force Redraw (The Fix)
zle -R
# 3. Execute
zle .accept-line
fi
}

for i in {1..5}; do
eval "_bsh_run_$i() { _bsh_run_idx $i; }; zle -N _bsh_run_$i"
done

# Bindings
bindkey '^[1' _bsh_run_1; bindkey '¡' _bsh_run_1
bindkey '^[2' _bsh_run_2; bindkey '™' _bsh_run_2
bindkey '^[3' _bsh_run_3; bindkey '£' _bsh_run_3
bindkey '^[4' _bsh_run_4; bindkey '¢' _bsh_run_4
bindkey '^[5' _bsh_run_5; bindkey '∞' _bsh_run_5