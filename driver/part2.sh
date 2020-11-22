#!/bin/zsh -f

check_post_file () {
    f=$1
    relpath=$(realpath --relative-to=. $f)
    reldirpath=$(realpath --relative-to=. $PROXY_DIR)
    echolog 3 "Fetching $(tinyurl cgi-bin/post) with the contents of $relpath" \
            "as POST data; this URL answers with what is submitted as POST payload," \
            "and we will compare this reply with the original file."
    if ! download_proxy $PROXY_DIR $(tinyurl "cgi-bin/post") \
         --data-binary @$f; then
        echoko "Download failed (more details in driver.log)."
        return 2
    else
        diff_file=$DIFF_DIR/post-${f:t}.diff
        if ! difflog $PROXY_DIR/post $f; then
            echoko "Files differ."
            echo "     Diff stored in driver.log" >&2
            return 1
        else
            echook "Files are identical."
            return 0
        fi
    fi

}


part2a() {
    echopart "*** PART 2a: POST (correctness)"

    start_tiny
    start_proxy

    num_succeeded=0
    num_run=0
    for f in $POST_LIST; do
        (( num_run++ ))
        echolog "$num_run: $f"
        clear_dirs
        
        if check_post_file $f; then
            (( num_succeeded++ ))
        fi 
    done

    kill_tiny
    kill_proxy

    (( post_correct_score = MAX_POST_CORRECT * num_succeeded / num_run ))
    echosubscore "PART 2a SCORE" "$post_correct_score/$MAX_POST_CORRECT"
}

part2b () {
    ######################
    # POST robustness

    echopart "*** PART 2b: POST (robustness, syntax)"

    start_tiny
    start_proxy

    num_succeeded=0
    num_run=0

    for file in $POST_ERROR_LIST; do
        (( ++num_run ))
        echolog "$num_run: $file"
        clear_dirs
        
        if check_returns_ko $file; then
            echolog "Proxy correctly didn't return page; trying a random correct" \
                    "page to see if proxy still working."
            # echoko and error message will be printed by check_post_file
            if check_post_file $POST_LIST[$((RANDOM % $#POST_LIST + 1))]; then
                (( ++num_succeeded ))
            fi
        fi

        echolog ""
    done

    kill_tiny
    kill_proxy

    (( post_rob_synt_score = MAX_POST_ROB_SYNT * num_succeeded / num_run ))
    echosubscore "PART 2b SCORE" "$post_rob_synt_score/$MAX_POST_ROB_SYNT"
}

part2c () {
    echopart "*** PART 2c: POST (robustness, connection)"

    
    pass=0
    header=driver/robustness_files/header_for_post

    start_tiny

    echolog ""
    echolog "1. Test that proxy survives when client dies while sending payload."
    start_proxy
    cat_withtinyport $header | nc -w 1 -q 1 localhost $proxy_port &> /dev/null

    ## Note that I don't care for the return data from the proxy.
    echolog 3 "Sent a short count payload, now trying a random correct page to see" \
            "if proxy still working."
    # echoko and error message will be printed by check_post_file
    if check_post_file $POST_LIST[$((RANDOM % $#POST_LIST + 1))]; then
        (( pass++ ))
    fi
    kill_proxy

    start_proxy
    echolog ""
    echolog "2. Test that proxy survives a dying web server while sending payload."

    ( cat_withtinyport $header
      dd if=/dev/urandom ibs=10K count=1 2> /dev/null
      sleep 4
      dd if=/dev/urandom ibs=100K count=1 2> /dev/null ) | \
        nc localhost $proxy_port > /dev/null &

    pid=$!
    sleep 2
    kill_tiny
    wait $pid 2> /dev/null
    start_tiny
    echolog 3 "Sent some POST data, and killed server in the middle. Now trying a random" \
            "correct page to see if proxy still working."
    if check_post_file $POST_LIST[$((RANDOM % $#POST_LIST + 1))]; then
        (( pass++ ))
    fi
    kill_tiny

    (( post_rob_conn_score = MAX_POST_ROB_CONN * pass / 2 ))
    echosubscore "PART 2c SCORE" "$post_rob_conn_score/$MAX_POST_ROB_CONN"
}


################################################################################
# POST
#
part2 () {
    echopart "* PART 2: POST"

    echolog ""
    part2a
    echolog ""
    part2b
    echolog ""
    part2c

    (( post_score = post_rob_conn_score + post_rob_synt_score + post_correct_score ))

    echolog ""
    echoscore "PART 2 SCORE" "$post_score/$MAX_POST"
}
