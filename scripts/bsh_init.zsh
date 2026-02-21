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


typeset -gA _bsh_suggestions
typeset -g _bsh_start_time
typeset -g _bsh_current_cmd
typeset -g _bsh_mode=0 # 0=Global, 1=Directory, 2=Branch
typeset -g _bsh_cycle_direction=1 # 1=Forward, -1=Backward (Fixes the skip bug)
typeset -g _bsh_selection_idx=-1
typeset -g _bsh_original_query=""
_bsh_filter_success=0

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

zmodload zsh/net/socket
zmodload zsh/datetime

typeset -g _bsh_sock_path
if [[ -n "$XDG_RUNTIME_DIR" ]]; then
    _bsh_sock_path="$XDG_RUNTIME_DIR/bsh.sock"
else
    _bsh_sock_path="/tmp/bsh_$(id -u).sock"
fi

_bsh_ensure_daemon() {
    if ! pgrep -x "bsh-daemon" > /dev/null; then
        if command -v bsh-daemon >/dev/null 2>&1; then
            bsh-daemon &!
        elif [[ -x "$BSH_REPO_ROOT/bin/bsh-daemon" ]]; then
            "$BSH_REPO_ROOT/bin/bsh-daemon" &!
        elif [[ -x "$BSH_REPO_ROOT/build/bsh-daemon" ]]; then
            "$BSH_REPO_ROOT/build/bsh-daemon" &!
        fi
        sleep 0.1 # Allow socket creation
    fi
}

_bsh_toggle_success_filter() {
    if [[ $_bsh_filter_success -eq 0 ]]; then _bsh_filter_success=1; else _bsh_filter_success=0; fi
    _bsh_refresh_suggestions
    zle redisplay
}
zle -N _bsh_toggle_success_filter
bindkey '^F' _bsh_toggle_success_filter

_bsh_refresh_suggestions() {
    _bsh_selection_idx=-1
    _bsh_original_query="$BUFFER"

    if [[ -z "${BUFFER// }" ]]; then
        POSTDISPLAY=""
        return
    fi

    _bsh_ensure_daemon

    local scope="global"
    local ctx="$PWD"
    if [[ $_bsh_mode -eq 1 ]]; then scope="dir"; fi
    if [[ $_bsh_mode -eq 2 ]]; then scope="branch"; fi

    if ! zsocket "$_bsh_sock_path" 2>/dev/null; then 
        POSTDISPLAY=""
        return
    fi
    local fd=$REPLY

    local delim=$'\x1F'

    # Send IPC message: SUGGEST \x1F query \x1F scope \x1F context \x1F success \x1F term_width
    local msg="SUGGEST\x1F${BUFFER}\x1F${scope}\x1F${ctx}\x1F${_bsh_filter_success}\x1F${COLUMNS:-80}"
    print -u $fd -n "$msg"

    local line
    local parsing_box=0
    local box_str=""
    local i=0
    _bsh_suggestions=()

    while read -r -u $fd line; do
        line="${line%$'\r'}"
        if [[ "$line" == "##SKIP##" ]]; then
            exec {fd}<&-
            if [[ $_bsh_cycle_direction -eq -1 ]]; then _bsh_mode=1; else _bsh_mode=0; fi
            _bsh_refresh_suggestions
            return
        elif [[ "$line" == "##BOX##" ]]; then
            parsing_box=1
        elif [[ $parsing_box -eq 0 ]]; then
            _bsh_suggestions[$i]="$line"
            ((i++))
        else
            box_str+="$line"$'\n'
        fi
    done
    exec {fd}<&- 

    if [[ ${#_bsh_suggestions[@]} -eq 0 ]]; then
        POSTDISPLAY=""
    else
        POSTDISPLAY="${box_str%$'\n'}"
    fi
}

_bsh_preexec() { _bsh_current_cmd="$1"; _bsh_start_time=$EPOCHREALTIME; }
_bsh_precmd() {
    local exit_code=$?
    if [[ -z "$_bsh_start_time" || -z "$_bsh_current_cmd" ]]; then return; fi
    local now=$EPOCHREALTIME; local duration=$(( (now - _bsh_start_time) * 1000 ))
    local cmd_log="$_bsh_current_cmd"
    _bsh_start_time=""; _bsh_current_cmd=""

    if zsocket "$_bsh_sock_path" 2>/dev/null; then
        local fd=$REPLY
        local delim=$'\x1F'
        local msg="RECORD${delim}${cmd_log}${delim}$$$delim${PWD}${delim}${exit_code}${delim}${duration%.*}"
        print -r -u $fd -n -- "$msg"
        read -t 0.05 -u $fd # Clear the OK response to avoid broken pipes
        exec {fd}<&-
    fi
}
autoload -Uz add-zsh-hook
add-zsh-hook preexec _bsh_preexec
add-zsh-hook precmd _bsh_precmd

_bsh_cycle_mode_fwd() { 
    _bsh_cycle_direction=1 
    (( _bsh_mode = (_bsh_mode + 1) % 3 ))
    _bsh_refresh_suggestions
    zle -R 
}
_bsh_cycle_mode_back() { 
    _bsh_cycle_direction=-1 
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


_bsh_self_insert() { zle .self-insert; _bsh_refresh_suggestions; }
zle -N self-insert _bsh_self_insert
_bsh_backward_delete_char() { zle .backward-delete-char; _bsh_refresh_suggestions; }
zle -N backward-delete-char _bsh_backward_delete_char

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

_bsh_cycle_up() {
    if [[ ${#_bsh_suggestions[@]} -eq 0 ]]; then
        zle up-line-or-history
        return
    fi

    local next_idx=$((_bsh_selection_idx + 1))
    
    if [[ -n "${_bsh_suggestions[$next_idx]}" ]]; then
        _bsh_selection_idx=$next_idx
        BUFFER="${_bsh_suggestions[$_bsh_selection_idx]}"
        CURSOR=$#BUFFER
    fi
}

_bsh_cycle_down() {
    if [[ ${#_bsh_suggestions[@]} -eq 0 ]]; then
        zle down-line-or-history
        return
    fi

    local prev_idx=$((_bsh_selection_idx - 1))

    if [[ $prev_idx -ge 0 ]]; then
        _bsh_selection_idx=$prev_idx
        BUFFER="${_bsh_suggestions[$_bsh_selection_idx]}"
        CURSOR=$#BUFFER
    elif [[ $prev_idx -eq -1 ]]; then
        _bsh_selection_idx=-1
        BUFFER="$_bsh_original_query"
        CURSOR=$#BUFFER
    fi
}

zle -N _bsh_cycle_up
zle -N _bsh_cycle_down

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

# Paste the following into your .zshrc if you want to use the up/down arrow keys for cycling through suggestions instead of history:
# bindkey '^[[A' _bsh_cycle_up
# bindkey '^[[B' _bsh_cycle_down