#!/bin/zsh -f

partBa () {
    echopart "*** PART Ba: CACHE (Tiny dying)"

    if (( HAS_TINY )); then
        echolog 3 "Can't check this part with existing Tiny.  Remove -t to test bonus."
        wait_a_bit
        return
    fi

    pass=0
    start_tiny
    start_proxy
    clear_dirs

    echolog 3 "Fetching a few files from the server."

    for file in $CACHE_LIST; do
        if ! download_proxy $PROXY_DIR $(tinyurl $file); then
            echoko "Failed to download $file"
            kill_tiny
            kill_proxy
            return
        fi
    done

    kill_tiny

    # Now try to fetch a cached copy of one of the fetched files.
    echolog ""
    echolog 3 "Fetching again $(tinyurl $FETCH_FILE), but with a dead webserver.  This" \
            "page should be retrieved from the cache, so the proxy shouldn't block/die."
    echolog ""

    if ! download_proxy $PROXY_DIR $(tinyurl $FETCH_FILE); then
        echoko "The proxy returned an unexpected error (driver.log may have more)."
    else
        # See if the proxy fetch succeeded by comparing it with the original
        # file in the tiny directory
        echolog "Comparing the files fetched when the webserver was alive and dead." 
        if  diff -q $PROXY_DIR/$FETCH_FILE html/$FETCH_FILE &>> $LOG; then
            echook "Files are identical."
            pass=1
        else
            echoko "Files differ.  Diff store in driver.log."
        fi
    fi

    kill_proxy

    (( cache_tiny_dies_score = MAX_CACHE_TINY_DIES * pass ))
    echosubscore "PART Ba SCORE" "$cache_tiny_dies_score/$MAX_CACHE_TINY_DIES"
}

check_cache_queries () {
    for query in $cache_queries; do
        q=("${(@s/:/)query}")
        echolog -n "Fetching file number $q[1]... "
        file=$dl_files[q[1]]

        if ! download_proxy $PROXY_DIR $(tinyurl $file); then
            echoko "Failed to download $(tinyurl $file) (driver.log may have more)"
            return 1
        fi
        
        if (( q[2] != 2 )); then
            echolog "Checking that this was$( (( q[2] )) && echo " not" ) cached."
            cmp $PROXY_DIR/${file:t} $PROXY_DIR/${file:t}.cached &>> $LOG
            if (( ? == q[2] )); then
                echook "Success."
            else
                echoko "Failed! (driver.log may have more.)"
                return 1
            fi
        else
            echolog ""
        fi
        mv $PROXY_DIR/${file:t} $PROXY_DIR/${file:t}.cached
    done
    return 0
}

partBb () {
    echopart "*** PART Bb: CACHE (400K files)"
    start_tiny
    start_proxy

    echolog ""
    echolog 3 "Fetching 3 files of 400KB, repeatedly, at least one of them should be" \
            "out of the cache."
    echolog ""

    dl_files=('cgi-bin/gen-rnd?size=400K&tag=1'
              'cgi-bin/gen-rnd?size=400K&tag=2'
              'cgi-bin/gen-rnd?size=400K&tag=3')
    cache_queries=(1:2 2:2 3:2 3:0 2:0 1:1 3:1 2:1 3:0)

    if check_cache_queries; then
        pass=1
    else
        pass=0
    fi
    
    kill_tiny
    kill_proxy

    (( cache_400K_score = MAX_CACHE_400K * pass ))
    echosubscore "PART Bb SCORE" "$cache_400K_score/$MAX_CACHE_400K"
}

partBc () {
    echopart "*** PART Bc: CACHE (90K files)"

    if (( HAS_TINY )); then
        echolog 3 "Can't check this part with existing Tiny.  Remove -t to test bonus."
        wait_a_bit
        return
    fi

    start_tiny
    start_proxy

    echolog ""
    echolog 3 "Fetching 12 files of 90KB, repeatedly, only one of them should be" \
            "out of the cache."
    echolog ""

    dl_files=()
    cache_queries=()
    for i in {1..12}; do
        dl_files+="cgi-bin/gen-rnd?size=90K&tag=$i"
        cache_queries+=($i:2)
    done
    
    cache_queries+=( # In cache now: 2-12
        12:0
        1:1
        1:0
        12:0        # In cache now: 1,3-12
        12:0
        3:0
        7:0
        9:0
        2:1         # In cache now 1-3,5-12
        6:0
        4:1         # In cache now 1-4,6-12
        5:1         # In cache now 1-7,9-12
        7:0)

    if check_cache_queries; then
        pass=1
    else
        pass=0
    fi

    kill_tiny
    if (( pass )); then
        echolog ""
        echolog 3 "Checking that the proxy caches based on the whole URL by starting a new Tiny" \
                "and querying a similar URL but with a different port (shouldn't be cached.)"
        echolog ""
        start_tiny
        
        cache_queries=(7:1)
        if check_cache_queries; then
            pass=1
        else
            pass=0
        fi
        kill_tiny
    fi

    kill_proxy

    (( cache_90K_score = MAX_CACHE_90K * pass ))
    echosubscore "PART Bc SCORE" "$cache_90K_score/$MAX_CACHE_90K"
}

partBd () {
    echopart "*** PART Bd: CACHE (check that big objects are not stored)"

    if (( HAS_PROXY )); then
        echolog 3 "Can't check this part with existing proxy.  Remove -p to test bonus." \
                "This is because we are imposing memory limitations to the proxy, which" \
                "your provided proxy do not have.  The driver will continue, but no" \
                "point will be awarded."
        wait_a_bit
    fi

    PROXY_PREFIX=(cgexec -g memory:100M)
    pass=0

    start_tiny
    start_proxy

    echolog 3 "Fetching 3 files of size 100KB, 2MB, 100KB, and make sure only" \
            "the first and third are cached.  Then fetch a file of 200MB; if it is"\
            "stored in memory, the proxy will be out of memory."
    echolog ""

    dl_files=('cgi-bin/gen-rnd?size=100K'
              'cgi-bin/gen-rnd?size=2M'
              'cgi-bin/gen-rnd?size=400K'
              'cgi-bin/gen-rnd?size=200MB')
    cache_queries=(1:2 2:2 3:2 2:1 1:0 3:0 2:1 4:2 3:0 4:1)

    if check_cache_queries; then
        if ! (( HAS_PROXY )); then
            pass=1
        fi
    fi

    kill_tiny
    kill_proxy
    PROXY_PREFIX=()
    clear_dirs
    
    (( cache_big_score = MAX_CACHE_BIG * pass ))
    echosubscore "PART Bd SCORE" "$cache_big_score/$MAX_CACHE_BIG"    
}
    

partBe () {
    echopart "*** PART Be: POLL-POST (check that proxy polls between client and server)"
    echolog ""
    echolog 3 "A connection will be opened, via proxy, to a page that just returns" \
            "the POST payload.  The payload will stall, and we're gonna check that" \
            "the web server still returns the partial payload."
    echolog ""

    start_proxy
    start_tiny

    rm -f $PROXY_DIR/fifo
    mkfifo $PROXY_DIR/fifo
    ( trap - TERM
      nc -I 1 -O 1 -w 5 localhost $proxy_port < $PROXY_DIR/fifo > $PROXY_DIR/post ) &
    pid=$!
    exec {fd}> $PROXY_DIR/fifo
    cat <<EOF | tocrlf >&$fd
POST http://localhost:$tiny_port/cgi-bin/post HTTP/1.0
Content-Length: 10000000

EOF
    dd if=/dev/urandom ibs=500K count=1 2> /dev/urandom >&$fd
    exec {fd}>&-
    sleep 0.2
    size=$(stat --printf="%s" $PROXY_DIR/post)
    kill $pid 2> /dev/null
    wait $pid 2> /dev/null
    
    kill_proxy
    kill_tiny

    if [[ $size -gt 100000 ]]; then
        echook "At least 100K transferred."
        pass=1
    else 
        pass=0
        echoko "Only $size bytes transferred, less than 100K; do not buffer that much."
    fi

    (( post_chunk_score = MAX_POST_CHUNK * pass ))
    echosubscore "PART Be SCORE" "$post_chunk_score/$MAX_POST_CHUNK"
}


################################################################################
# Caching: Bonus
#

partB () {
    echopart "* BONUS PART: CACHE & POLL-POST"

    echolog ""
    partBa
    echolog ""
    partBb
    echolog ""
    partBc
    echolog ""
    partBd
    echolog ""
    partBe
    echolog ""

    (( bonus_score = cache_tiny_dies_score + cache_400K_score + \
                   cache_90K_score + cache_big_score + post_chunk_score))
    echoscore "BONUS PART SCORE"  "$bonus_score/$MAX_BONUS"
}
################################################################################
