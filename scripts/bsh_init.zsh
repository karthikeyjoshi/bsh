# scripts/bsh_init.zsh

local BSH_INIT_SCRIPT_PATH="${(%):-%N}"
export BSH_REPO_ROOT="$(dirname $(dirname $BSH_INIT_SCRIPT_PATH))"

if [[ -x "$BSH_REPO_ROOT/bin/bsh" ]]; then
    export BSH_BINARY="$BSH_REPO_ROOT/bin/bsh"
elif [[ -x "$BSH_REPO_ROOT/build/bsh" ]]; then
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
typeset -g _bsh_cycle_direction=1 # 1=Forward, -1=Backward (Fixes the skip bug)
_bsh_filter_success=0

# --- DAEMON MANAGER ---
_bsh_ensure_daemon() {
    if ! pgrep -x "bsh-daemon" > /dev/null; then
        if [[ -x "$BSH_REPO_ROOT/bin/bsh-daemon" ]]; then
            "$BSH_REPO_ROOT/bin/bsh-daemon" &!
        elif [[ -x "$BSH_REPO_ROOT/build/bsh-daemon" ]]; then
            "$BSH_REPO_ROOT/build/bsh-daemon" &!
        fi
    fi
}

_bsh_toggle_success_filter() {
    if [[ $_bsh_filter_success -eq 0 ]]; then _bsh_filter_success=1; else _bsh_filter_success=0; fi
    _bsh_refresh_suggestions
    zle redisplay
}
zle -N _bsh_toggle_success_filter
bindkey '^F' _bsh_toggle_success_filter

# --- SUGGESTION ENGINE ---
_bsh_refresh_suggestions() {
    _bsh_ensure_daemon
    if [[ ! -x "$BSH_BINARY" ]]; then return; fi
    if [[ -z "${BUFFER// }" ]]; then
        POSTDISPLAY=""
        return
    fi

    # 1. Prepare Arguments
    local args=("$BUFFER" "--scope")
    local header_text=" BSH: Global "
    
    if [[ $_bsh_mode -eq 1 ]]; then
        args+=("dir" "--cwd" "$PWD")
        header_text=" BSH: Directory "
    elif [[ $_bsh_mode -eq 2 ]]; then
        args+=("branch" "--cwd" "$PWD")
        header_text=" BSH: Branch " 
    else
        args+=("global")
    fi

    if [[ $_bsh_filter_success -eq 1 ]]; then
        args+=("--success")
        header_text="${header_text% } [OK] "
    fi

    # 2. Execution
    local output
    output=$("$BSH_BINARY" suggest "${args[@]}" 2>/dev/null)
    if [[ -z "$output" ]]; then
        POSTDISPLAY=""
        _bsh_suggestions=()
        return
    fi

    # 3. Parse Output & Handle Metadata
    _bsh_suggestions=()
    local -a display_lines
    local -a raw_lines
    raw_lines=("${(@f)output}") 

    local i=0
    for line in "${raw_lines[@]}"; do
        # --- SMART SKIP LOGIC ---
        if [[ "$line" == "##BRANCH:"* ]]; then
            local branch_name="${line##*:}"
            
            # If Branch is 'unknown' (not a repo), we must skip this page.
            if [[ "$branch_name" == "unknown" || -z "$branch_name" ]]; then
                if [[ $_bsh_mode -eq 2 ]]; then
                    # FIX: Use cycle direction to decide where to skip
                    if [[ $_bsh_cycle_direction -eq -1 ]]; then
                        # Moving Backward (Global -> Branch -> Directory)
                        _bsh_mode=1
                    else
                        # Moving Forward (Directory -> Branch -> Global)
                        _bsh_mode=0
                    fi
                    
                    # RECURSIVE CALL: Refresh immediately with new mode
                    _bsh_refresh_suggestions
                    return
                fi
            else
                 header_text=" BSH: Branch ($branch_name) "
            fi
            continue 
        fi

        # --- PROCESS SUGGESTION ---
        [[ $i -ge 5 ]] && break
        [[ -z "${line// }" ]] && continue

        _bsh_suggestions[$i]="$line"
        local display_num=$((i + 1))
        local text=" $display_num: $line"
        
        # Truncation
        local term_width=${COLUMNS:-80}
        local safe_text_limit=$((term_width - 7))
        if (( ${#text} > safe_text_limit )); then
            text="${text:0:$((safe_text_limit - 3))}..."
        fi

        display_lines+=("$text")
        ((i++))
    done

    # 4. Box Drawing
    local max_len=${#header_text}
    for line in "${display_lines[@]}"; do
         local clean_text=${line//$'\e'[\[(]*([0-9;])[@-~]/}
         if (( ${#clean_text} > max_len )); then max_len=${#clean_text}; fi
    done
    max_len=$((max_len + 4))

    if [[ ${#display_lines[@]} -eq 0 ]]; then
        POSTDISPLAY=""
        return
    fi

    local result=$'\n'
    local top_content="╭$header_text"
    result+="${(r:max_len+1::─:)top_content}╮"

    for line in "${display_lines[@]}"; do
        result+=$'\n'
        result+="│${(r:max_len:: :)line}│"
    done
    result+=$'\n'
    local bot_content="╰"
    result+="${(r:max_len+1::─:)bot_content}╯"

    POSTDISPLAY="$result"
}

# --- STATE SWITCHER ---
_bsh_cycle_mode_fwd() { 
    _bsh_cycle_direction=1  # Set Direction Forward
    (( _bsh_mode = (_bsh_mode + 1) % 3 ))
    _bsh_refresh_suggestions
    zle -R 
}
_bsh_cycle_mode_back() { 
    _bsh_cycle_direction=-1 # Set Direction Backward
    (( _bsh_mode = _bsh_mode - 1 ))
    if (( _bsh_mode < 0 )); then _bsh_mode=2; fi
    _bsh_refresh_suggestions
    zle -R 
}

zle -N _bsh_cycle_mode_fwd
zle -N _bsh_cycle_mode_back
bindkey "^[f" _bsh_cycle_mode_fwd
bindkey "^[[1;3C" _bsh_cycle_mode_fwd
bindkey "^[b" _bsh_cycle_mode_back
bindkey "^[[1;3D" _bsh_cycle_mode_back

# --- HOOKS ---
_bsh_preexec() { _bsh_current_cmd="$1"; zmodload zsh/datetime; _bsh_start_time=$EPOCHREALTIME; }
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

_bsh_self_insert() { zle .self-insert; _bsh_refresh_suggestions; }
zle -N self-insert _bsh_self_insert
_bsh_backward_delete_char() { zle .backward-delete-char; _bsh_refresh_suggestions; }
zle -N backward-delete-char _bsh_backward_delete_char

# --- BINDINGS ---
_bsh_accept_line() { POSTDISPLAY=""; zle -R; zle .accept-line; }
zle -N accept-line _bsh_accept_line

_bsh_run_idx() {
    local idx=$(($1 - 1))
    if [[ -n "${_bsh_suggestions[$idx]}" ]]; then
        BUFFER="${_bsh_suggestions[$idx]}"
        POSTDISPLAY=""; zle -R; zle .accept-line
    fi
}

_bsh_insert_idx() {
  local idx=$(($1 - 1))
  if [[ -n "${_bsh_suggestions[$idx]}" ]]; then
    BUFFER="${_bsh_suggestions[$idx]}" 
    CURSOR=$#BUFFER                    
    POSTDISPLAY=""                     
    zle -R                             
  fi
}

for i in {1..5}; do 
  eval "_bsh_insert_$i() { _bsh_insert_idx $i; }; zle -N _bsh_insert_$i"
done
for i in {1..5}; do eval "_bsh_run_$i() { _bsh_run_idx $i; }; zle -N _bsh_run_$i"; done

bindkey '^[1' _bsh_run_1; bindkey '¡' _bsh_run_1
bindkey '^[2' _bsh_run_2; bindkey '™' _bsh_run_2
bindkey '^[3' _bsh_run_3; bindkey '£' _bsh_run_3
bindkey '^[4' _bsh_run_4; bindkey '¢' _bsh_run_4
bindkey '^[5' _bsh_run_5; bindkey '∞' _bsh_run_5

# Standard Linux/raw bindings (Alt + Shift + 1-5)
bindkey "^[!" _bsh_insert_1
bindkey "^[@" _bsh_insert_2
bindkey "^[#" _bsh_insert_3
bindkey "^[$" _bsh_insert_4
bindkey "^[%" _bsh_insert_5

# macOS specific bindings (Option + Shift + 1-5)
bindkey '⁄' _bsh_insert_1  
bindkey '€' _bsh_insert_2
bindkey '‹' _bsh_insert_3
bindkey '›' _bsh_insert_4
bindkey 'ﬁ' _bsh_insert_5