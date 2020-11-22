#!/bin/zsh -f

check_proxy_threaded () {
    echolog "Checking that the proxy is threaded."
    start_proxy
    sleep 0.5 ## Make sure the proxy has time to create the threads.
    nthreads=$(sed -n '/^Threads/ {s/.*:[[:space:]]*//;p;q}' /proc/$proxy_pid/status)
    kill_proxy
    if [[ $nthreads != $((NTHREADS + 1)) ]]; then
        echoko "The proxy does not have precisely $((NTHREADS + 1)) threads." \
               "The driver will continue but no point for this part will be awarded" \
               "until this is fixed."
        return 1
    fi
}

part3a () {
    echopart "*** PART 3a: CONCURRENCY (two files in parallel)"

    start_tiny
    start_proxy
    start_nop

    pass=0

    # Try to fetch a file from the blocking nop-server using the proxy
    clear_dirs
    result=0
    echolog 3 "Fetching two files in parallel, one from a blocking server, and a second" \
            "from Tiny; only the second one should be served."
    echolog ""
    echolog 3 "Fetching a file from blocking nop-server"

    ( trap - TERM
      download_proxy $PROXY_DIR \
                     "http://localhost:$nop_port/nop-file.txt" \
                     --max-time 5 ) &
    pid=$!
    sleep 1     # Wait for proxy to be properly stuck
    check_proxy_noproxy $FETCH_FILE $(tinyurl "$FETCH_FILE") --max-time 2
    result=$?
    if ! kill -0 $pid 2> /dev/null; then
        echoko "The blocking request died in the meantime."
    else
        pass=1
    fi

    kill_nop ## This will free the proxy's first thread
    kill $pid 2> /dev/null
    wait $pid 2> /dev/null

    kill_tiny
    kill_proxy

    (( conc_two_score = MAX_CONC_TWO * pass ))
    echosubscore "PART 3a SCORE" "$conc_two_score/$MAX_CONC_TWO"
}

NTHREADS=64

part3b () {
    echopart "*** PART 3b: CONCURRENCY (many files in parallel)"
    echolog ""
    echolog 3 "Your proxy should serve $NTHREADS connections in parallel, no more, no less." \
            "Connections beyond the ${NTHREADS}-th should be queued and only served once another " \
            "connection terminates.  To test this, we now now create $((2 * NTHREADS + 1)) connections." \
            "Client number $((NTHREADS + 1)) should stall and not be served.  The first $NTHREADS should" \
            "be able to query data in any order; once they're all served, the last $NTHREADS" \
            "will do the same."
    echolog ""

    start_tiny
    start_proxy

    clear_dirs

    pass=0
    OK=1
    pids=()
    pipes=()
    for i in {1..$((2 * NTHREADS))}; do
        if (( i == $NTHREADS + 1)); then
            ## Here, we check that the server indeed stalls.
            echolog "Checking that proxy stalls the ${i}-th connection."
            download_proxy $PROXY_DIR $(tinyurl "$FETCH_FILE") --max-time 1
            if (( $? != 1)); then
                echoko "Failed: the page was served."
                OK=0
                break
            fi
        fi

        rm -f $PROXY_DIR/fifo-$i
        mkfifo $PROXY_DIR/fifo-$i
        (   ## !! Close the pipes in this subshell
            trap - TERM
            for fd in $pipes; do
                exec {fd}>&-
            done
            nc -w 5 localhost $proxy_port < $PROXY_DIR/fifo-$i  | \
                sed -n '/^\r$/{:a n;p;ba;}' > $PROXY_DIR/concurrency-$i ) &
        pids+=$!
        ## Open the pipe, and start sending some text it
        integer fd
        exec {fd}> $PROXY_DIR/fifo-$i
        pipes+=$fd
        echo -n 'GET ' >&$fd
    done

    if (( OK )); then
        echolog "Started $((NTHREADS * 2)) extra clients, having them served."
        for i in $(seq $NTHREADS -1 $((NTHREADS / 2)))  \
                     $(seq 1 $((NTHREADS / 2 - 1)) | sort -R) \
                     $(seq $((NTHREADS + 1)) $((NTHREADS * 2)) | sort -R); do
            if ! kill -0 $pids[i] 2> /dev/null; then
                echoko "A client timedout, the proxy is probably not able to manage $NTHREADS clients" \
                       "at once or is too slow in doing so."
                OK=0
                break
            fi
            fd=$pipes[i]
            echo -ne "$(tinyurl $FETCH_FILE) HTTP/1.0\r\n\r\n" >&$fd
            exec {fd}>&-
            wait $pids[i]
            if ! cmp $PROXY_DIR/concurrency-$i html/$FETCH_FILE &> /dev/null; then
                OK=0
                echoko "Failed: page fetched differs from expected."
                break
            fi
        done
    fi

    for i in $pids; do
        kill $i 2> /dev/null
        wait $i 2> /dev/null
    done

    if (( OK )); then
        echook "Passed."
        pass=1
    fi

    kill_tiny
    kill_proxy
    (( conc_many_score = MAX_CONC_MANY * pass ))
    echosubscore "PART 3b SCORE" "$conc_many_score/$MAX_CONC_MANY"
}

part3 () {
    echopart "* PART 3: CONCURRENCY"

    if ! check_proxy_threaded; then
        wait_a_bit
        threaded=0
    else
        threaded=1
    fi

    echolog ""
    part3a
    echolog ""
    part3b

    echolog ""
    (( conc_score = threaded * (conc_two_score + conc_many_score) ))
    echoscore "PART 3 SCORE" "$conc_score/$MAX_CONC"
}
