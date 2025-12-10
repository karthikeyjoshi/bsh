# scripts/bsh_init.zsh

BSH_BINARY="$HOME/bsh/build/bsh"

# State Variables
typeset -gA _bsh_suggestions
typeset -g _bsh_start_time
typeset -g _bsh_current_cmd
typeset -g _bsh_mode=0        # 0=Global, 1=Directory, 2=Branch

# --- GIT HELPER ---
_bsh_get_branch() {
    git rev-parse --abbrev-ref HEAD 2>/dev/null
}

# --- SUGGESTION ENGINE ---
_bsh_refresh_suggestions() {
    if [[ ! -x "$BSH_BINARY" ]]; then return; fi
    if [[ -z "$BUFFER" || ${#BUFFER} -lt 1 ]]; then
        POSTDISPLAY=""
        return
    fi

    local args=("$BUFFER" "--scope")
    local header_text=" BSH: Global "
    
    if [[ $_bsh_mode -eq 0 ]]; then
        args+=("global")
    elif [[ $_bsh_mode -eq 1 ]]; then
        args+=("dir" "--cwd" "$PWD")
        header_text=" BSH: Directory "
    elif [[ $_bsh_mode -eq 2 ]]; then
        local branch=$(_bsh_get_branch)
        if [[ -z "$branch" ]]; then
            _bsh_mode=0; args+=("global")
        else
            args+=("branch" "--branch" "$branch")
            header_text=" BSH: Branch ($branch) "
        fi
    fi

    local output
    output=$("$BSH_BINARY" suggest "${args[@]}")
    
    if [[ -z "$output" ]]; then
        POSTDISPLAY=""
        _bsh_suggestions=()
        return
    fi

    # Parse Output
    _bsh_suggestions=()
    local -a display_lines
    local max_len=${#header_text}
    local i=0
    
    echo "$output" | while read -r line; do
        [[ -z "$line" ]] && continue
        [[ $i -ge 5 ]] && break 

        _bsh_suggestions[$i]="$line"
        
        local display_num=$((i + 1))
        local text=" ⌥$display_num: $line"
        
        display_lines+=("$text")
        if (( ${#text} > max_len )); then max_len=${#text}; fi
        ((i++))
    done

    # Draw Box
    local result=$'\n'
    local top="╭$header_text"
    for ((k=${#header_text}; k<max_len+1; k++)); do top+="─"; done
    top+="╮"
    result+="$top"

    for line in "${display_lines[@]}"; do
        result+=$'\n'
        local pad=$(( max_len - ${#line} ))
        local padding=""
        for ((k=0; k<pad+1; k++)); do padding+=" "; done
        result+="│$line$padding│"
    done
    
    result+=$'\n'
    local bot="╰"
    for ((k=0; k<max_len+1; k++)); do bot+="─"; done
    bot+="╯"
    result+="$bot"

    POSTDISPLAY="$result"
}

# --- STATE SWITCHER ---
_bsh_cycle_mode_fwd() { (( _bsh_mode = (_bsh_mode + 1) % 3 )); _bsh_refresh_suggestions; zle -R; }
_bsh_cycle_mode_back() { (( _bsh_mode = (_bsh_mode - 1) )); if (( _bsh_mode < 0 )); then _bsh_mode=2; fi; _bsh_refresh_suggestions; zle -R; }
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