#!/bin/zsh -f

part1a () {
    ################################################################################
    # GET
    #
    echopart "*** PART 1a: GET (correctness)"

    num_run=0
    num_succeeded=0

    start_tiny
    start_proxy

    # Now do the test by fetching some text and binary files directly from
    # Tiny and via the proxy, and then comparing the results.
    for file in $GET_LIST; do
        (( ++num_run ))
        echolog "$num_run: $file"
        clear_dirs

        if check_proxy_noproxy $file; then
            (( ++num_succeeded ))
        fi

        echolog ""
    done

    kill_tiny
    kill_proxy

    (( get_correct_score = MAX_GET_CORRECT * num_succeeded / num_run ))
    echosubscore "PART 1a SCORE" "$get_correct_score/$MAX_GET_CORRECT"
}

part1b () {
    #########################################
    # GET headers

    echopart "*** PART 1b: GET (checking headers are correctly forwarded)."
    start_tiny
    start_proxy
    
    pass=0
    rnd=$RANDOM
    rndstr=$(sha512sum src/proxy | cut -d' ' -f1)
    extra_curl_args=(-H "Host: loremipsum.com")
    for i in {1..80}; do  # Let's be realistic; Apache 2.3 limits each field to
        # 8K and at most 100 headers.
        extra_curl_args+=(-H "X-$((rnd + i)): $RANDOM$rndstr")
    done

    if ! download_proxy $PROXY_DIR $(tinyurl "cgi-bin/env") $extra_curl_args || \
            ! download_noproxy $NOPROXY_DIR $(tinyurl "cgi-bin/env") $extra_curl_args; then
        echoko "Download failed (more details in driver.log)."
    else
        for f in $PROXY_DIR/env $NOPROXY_DIR/env; do
            sort -u $f | grep -iv '^\(User-Agent\|Connection\|Proxy-Connection\)=' > \
                              ${f}_clean
        done
        if ! grep -iq "^Proxy-Connection=close$" $PROXY_DIR/env; then
            echoko "The proxy did not set the Proxy-Connection header."
        elif ! difflog $PROXY_DIR/env_clean $NOPROXY_DIR/env_clean; then
            echoko "The proxy did not correctly forward the headers."
            echo "     Diff stored in driver.log" >&2
        else
            echook "Passed."
            pass=1
        fi
    fi

    kill_tiny
    kill_proxy

    (( get_header_score = MAX_GET_HEADER * pass ))
    echosubscore "PART 1b SCORE" "$get_header_score/$MAX_GET_HEADER"
}

part1c () {
    #########################################
    # GET robustness, syntax

    echopart "*** PART 1c: GET (robustness, syntax)"

    num_run=0
    num_succeeded=0

    start_tiny
    start_proxy
    
    for file in $GET_ERROR_LIST; do
        (( ++num_run ))
        echolog "$num_run: $file"
        clear_dirs
        
        if check_returns_ko $file; then
            echolog 3 "Proxy correctly didn't return page; trying a random correct" \
                    "page to see if proxy still working."
            # echoko and error message will be printed by check_proxy_noproxy
            if check_proxy_noproxy $GET_LIST[$((RANDOM % $#GET_LIST + 1))]; then
                (( ++num_succeeded ))
            fi
        fi

        echolog ""
    done

    kill_tiny
    kill_proxy

    (( get_rob_synt_score = MAX_GET_ROB_SYNT * num_succeeded / num_run ))
    echosubscore "PART 1c SCORE" "$get_rob_synt_score/$MAX_GET_ROB_SYNT"
}

part1d () {
    #########################################
    # GET robustness, connection

    echopart "*** PART 1d: GET (robustness, connection)"
    start_tiny
    
    pass=0
    echolog ""
    echolog "1. Test that proxy survives when remote server dies."
    start_proxy
    start_nop
    ( sleep 3; kill_nop )&
    nopsubshell=$!
    download_proxy $PROXY_DIR \
                   "http://localhost:$nop_port/nop-file.txt" \
                   --max-time 6
    if (( ? != 1 )); then
        echolog 3 "Proxy didn't get stuck by a dying remote server, good!" \
                "Trying a random correct page to see if proxy still working."
        ## KO/OK printed by check_proxy_noproxy
        if check_proxy_noproxy $GET_LIST[$((RANDOM % $#GET_LIST + 1))]; then
            pass=1
        fi
    else
        echoko "Proxy got stuck although remote server died."
    fi
    ## This waits for nopserver to die.
    wait $nopsubshell 2> /dev/null

    kill_proxy
    
    echolog ""
    echolog "2. Test that proxy survives when client dies."
    start_proxy
    echolog 3 "Sending a request for a webpage and killed client before answer received."
    ## Netcat to wait 1 second and quit.  cgi-bin/stall should stall for more than that.
    echo -en "GET $(tinyurl cgi-bin/stall) HTTP/1.0\r\n\r\n" | \
        nc -w 1 localhost $proxy_port > /dev/null
    sleep 3 # Wait for stall to return, no good way but this.
    echolog 3 "Now fetching a random correct page to see if proxy still working."
    if check_proxy_noproxy $GET_LIST[$((RANDOM % $#GET_LIST + 1))]; then
        (( ++pass ))
    fi

    kill_proxy
    kill_tiny

    (( get_rob_conn_score = MAX_GET_ROB_CONN * pass / 2 ))
    echosubscore "PART 1d SCORE" "$get_rob_conn_score/$MAX_GET_ROB_CONN"
}

part1e () {
    echopart "*** PART 1e: GET (check that the server's output is forwarded in chunks)"

    file="cgi-bin/gen-rnd?size=500K&pause"
    start_proxy
    start_tiny

    ( trap - TERM
      download_proxy $PROXY_DIR $(tinyurl $file) ) &
    pid=$!
    sleep 2
    size=$(stat --printf="%s" $PROXY_DIR/${file:t} 2>> $LOG)
    kill $! 2> /dev/null
    wait $! 2> /dev/null

    kill_proxy
    kill_tiny
    

    if [[ $size -gt 100000 ]]; then
        echook "At least 100K transferred."
        pass=1
    else
        echoko "Only $size bytes transferred, less than 100K; do not buffer that much."
        pass=0
    fi

    (( get_chunk_score = MAX_GET_CHUNK * pass ))
    echosubscore "PART 1e SCORE" "$get_chunk_score/$MAX_GET_CHUNK"
}

part1 () {
    echopart "* PART 1: GET"

    echolog ""
    part1a
    echolog ""
    part1b
    echolog ""
    part1c
    echolog ""
    part1d
    echolog ""
    part1e

    (( get_score = get_correct_score + get_header_score +
                get_rob_conn_score + get_rob_synt_score + get_chunk_score ))

    echolog ""
    echoscore "PART 1 SCORE" "$get_score/$MAX_GET"
}
