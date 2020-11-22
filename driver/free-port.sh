#!/bin/zsh -f

MAX_RAND=63000
PORT_START=1024
PORT_MAX=65000
MAX_PORT_TRIES=10

is_port_in_use () {
    portsinuse=$(netstat --numeric-ports --numeric-hosts -a --protocol=tcpip \
                     | grep tcp | cut -c21- | cut -d':' -f2 | cut -d' ' -f1 \
                     | grep -E "[0-9]+" | uniq | tr "\n" " ")

    echo "$portsinuse" | grep -wq "$1"
}

#
# wait_for_port_use - Spins until the TCP port number passed as an
#     argument is actually being used. Times out after 5 seconds.
#
wait_for_port_use () {
    timeout_count=0
    while ! is_port_in_use $1; do
        (( ++timeout_count ))
        if (( timeout_count == MAX_PORT_TRIES )); then
            kill -ALRM $$
        fi

        sleep 0.2
    done
}

free_port () {
    # Generate a random port in the range [PORT_START,
    # PORT_START+MAX_RAND]. This is needed to avoid collisions when many
    # students are running the driver on the same machine.
    port=$((( RANDOM % $MAX_RAND) + $PORT_START))

    while true; do
        if is_port_in_use $port; then
            if (( port == PORT_MAX )); then
                echo "-1"
                return
            fi
            (( ++port ))
        else
            echo "$port"
            return
        fi
    done
}

## If not sourced, call free_port
[[ $ZSH_EVAL_CONTEXT =~ :file$ ]] || free_port
