################################################################################
# Echo with color, and to log functions
tocrlf () {
    sed 's/\r*$/\r/'
}

echook() {
    echo -ne '\e[1m\e[38;5;77m[OK]\e[0m ' >&2
    echo -n 'OK ' >> $LOG
    echolog 5! "$@"
}
echoko() {
    echo -ne '\e[1;91m[KO]\e[0m ' >&2
    echo -n 'KO ' >> $LOG
    echolog 5! "$@"
}

echopart () {
    echo -ne "\e[1m\e[30;47m" >&2
    ## For main header, fill the line.
    [[ $1 =~ '^\* ' ]] && printf '%*s\r' $COLUMNS
    echolog -n "$@"
    echo -ne "\e[0m" >&2
    echolog ""
}

echoscore () {
    echo -ne '\e[1m\e[38;5;255m\e[48;5;21m' >&2
    printf '%*s\r' $COLUMNS
    echolog -n "$1"
    echo -ne "\e[0m" >&2
    echolog "  $2  "
}

echosubscore () {
    echo -ne '\e[1m\e[38;5;255m\e[48;5;21m' >&2
    echolog -n "$1"
    echo -ne "\e[0m" >&2
    echolog "  $2  "
}
   

SPACES='              '
echolog  () {
    case $1; in
        -n) # Don't mess with it.
            echo "$@" | tee -a $LOG >&2
            ;;
        [0-9]) ## Indent by that amount
            indent=$1
            (( indent-- ))
            shift
            echo "$SPACES[1,indent]" "$@" | fmt -ct | tee -a $LOG >&2
            ;;
        [0-9]!) ## Indent by that amount, but start at point.
            indent=${1[1]}
            (( indent-- ))
            shift
            echo "$SPACES[1,indent]" "$@" | fmt -ct | sed '1s/.\{'$indent'\}//' | tee -a $LOG >&2
            ;;
        *)
            echo "$@" | fmt -ct | tee -a $LOG >&2
    esac
}

difflog () {
    diff -u "$@" >> $LOG
}

################################################################################
# curl and downloads

tinyurl () {
    echo -n "http://localhost:$tiny_port/$1"
}

# call_curl dest_dir url extra_opts...
# returns 1 iff curl timedout, 2 on other errors.
call_curl () {
    cd $1
    curl --max-time $TIMEOUT --silent --show-error "${@:3}" --output ${2:t} $2 &>> $LOG
    curl_result=$?
    cd $HOME_DIR
    if (( curl_result == 28 )); then
        return 1
    elif (( curl_result )); then
        return 2
    fi
    return 0
}

download_proxy () {
    if ! is_port_in_use $proxy_port ||
            { ! (( HAS_PROXY )) && ! [[ -e /proc/$proxy_pid ]] }; then
        echolog "ERROR: Proxy is dead."
        return 1
    fi
    call_curl $1 $2 --proxy "http://localhost:$proxy_port" "${@:3}"
}

download_noproxy () {
    call_curl $1 $2 "${@:3}"
}

clear_dirs () {
    rm -fR $PROXY_DIR $NOPROXY_DIR
    mkdir -p $PROXY_DIR $NOPROXY_DIR
}

################################################################################
# Check that proxy and direct are the same, and check that expectfails are failing.

check_proxy_noproxy () {
    # Fetch using the proxy
    file=$1

    echolog 3 "Fetching $(tinyurl $file) via proxy"
    if ! download_proxy $PROXY_DIR $(tinyurl $file); then
        echoko "Couldn't fetch file via proxy (more details in driver.log)."
        return 1
    fi
    
    # Fetch directly from Tiny
    echolog 9 "...and directly from the webserver"
    if ! download_noproxy $NOPROXY_DIR $(tinyurl $file); then
        echoko "Couldn't fetch file from Tiny (this shouldn't happen)."
        return 2
    fi

    if difflog $PROXY_DIR/${file:t} $NOPROXY_DIR/${file:t}; then
        echook "Files are identical."
        return 0
    else
        echoko "Files differ.  Diff stored in driver.log"
        return 1
    fi
}

cat_withtinyport () {
    sed "s/TINYPORT/$tiny_port/g" $1 | tocrlf
}

check_returns_ko () {
    if cat_withtinyport $1 | nc -q 1 localhost $proxy_port | \
            grep -q 'HTTP/.*200'; then
        echoko "Proxy returned OK on KO trace!"
        return 1
    fi
    return 0;
}

################################################################################
# Starting and killing servers

get_pid_from_port () {
    pid=$(netstat --numeric-ports --numeric-hosts --protocol=tcpip -a -p 2> /dev/null | \
              sed -n "/:$1\b.*LISTEN/"' { s@.*LISTEN[[:space:]]*\([0-9]*\)/.*@\1@; p; q }')
    if ! [[ -e /proc/$pid/status ]]; then
        echolog "FATAL ERROR: Cannot find the PID of $2."
        exit 1
    fi
}

## start_listener varprefix friendlyname bin..
start_listener () {
    fport=$(free_port)
    theserver=$2
    echolog "Starting $theserver on port $fport"
    timeout 15 "${@:3}" $fport |& sed "s/^/${(U)2}: /" >> $LOG &
    wait_for_port_use $fport
    eval "${1}_port=$fport"
    get_pid_from_port $fport $theserver
    eval "${1}_pid=$pid"
}

## kill_listener varprefix
kill_listener () {
    eval "pid=\$${1}_pid"
    if (( pid )); then
        echolog "Killing $2"
        kill $pid 2> /dev/null
        wait $pid 2> /dev/null
        eval "${1}_pid=0"
    fi
}

start_tiny ()  {
    (( HAS_TINY )) || {
        cd html/
        start_listener tiny "Tiny" ../tiny/tiny
        cd $HOME_DIR
    }
}

kill_tiny ()   { kill_listener tiny "Tiny" }

start_proxy () {
    if (( HAS_PROXY )); then
        if [[ $proxy_pid == "" ]]; then
            get_pid_from_port $proxy_port "proxy"
            proxy_pid=$pid
        fi
    else
        bin=($PROXY_PREFIX[@] ./proxy)
        start_listener proxy "proxy" $bin
    fi
}
kill_proxy ()  { (( HAS_PROXY )) || kill_listener proxy "proxy" }

start_nop () { start_listener nop "blocking server" driver/nop-server.py }
kill_nop () { kill_listener nop "blocking server" }

kill_all () { kill_tiny; kill_proxy; kill_nop; pkill -x -G $$ nc 2> /dev/null; }
