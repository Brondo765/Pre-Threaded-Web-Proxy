#!/bin/zsh -f

# driver.sh - Autograder for the Proxy Lab.
#
#     David O'Hallaron, Carnegie Mellon University (till 2016)
#     Michaël Cadilhac, DePaul University (since Fall 2020, complete rewrite)
#
#     Usage: ./driver.sh [-h] [-p PROXY_PORT] [-t TINY_PORT] [-P PART]"
# 

# Various constants
HOME_DIR=$(pwd)
PROG=$0
PROXY_DIR="$HOME_DIR/.proxy"
NOPROXY_DIR="$HOME_DIR/.noproxy"
TINY_LOG="$HOME_DIR/tiny.log"
PROXY_LOG="$HOME_DIR/proxy.log"
LOG="$HOME_DIR/driver.log"

TIMEOUT=5

# List of text and binary files for the get test
setopt extended_glob

## These will be queried to Tiny, so should not include html/.
GET_LIST=(html/*(.:t)
          'cgi-bin/adder?42&51'
          'cgi-bin/adder?13&12'
          'cgi-bin/adder?333&1')
## These are netcat'ed into the proxy.
GET_ERROR_LIST=(driver/robustness_files/get_*~*~)

## These are used as the POST contents.
POST_LIST=(html/*(.:A) tiny/*(.:A)) # A for Absolute.  Pretty printed using
                                    # relative path.

## These are netcat'ed into the proxy.
POST_ERROR_LIST=(driver/robustness_files/post_*~*~)

# List of text files for the cache test
CACHE_LIST=(sus.png
            home.html
            lipsum.txt)

# The file we will fetch for various tests
FETCH_FILE=sus.png

#####
# Helper functions
#
source_safe () {
    if ! source $1; then
        echo "Error: $1 not found."
        exit
    fi
}

source_safe driver/part-points.sh
source_safe driver/free-port.sh
source_safe driver/check-env.sh
source_safe driver/extra.sh
source_safe driver/part1.sh
source_safe driver/part2.sh
source_safe driver/part3.sh
source_safe driver/partB.sh

#######
# Main 
#######

usage () {
    cat <<EOF
Usage: $PROG [-h] [-p PROXY_PORT] [-t TINY_PORT] [-P PART]"
Runs the driver for the proxylab.

  -h             Print this.
  -p PROXY_PORT  Use the proxy at localhost:PROXY_PORT throughout.
  -P PART        Only check specific part.  PART is for instance 1a, 2, or Bd.
                 Use "$PROG -P list" to list all parts.
  -t TINY_PORT   Use the Tiny webserver at localhost:TINY_PORT throughout.
EOF
    exit 1
}

while getopts "hp:t:P:x" opt; do
    case $opt; in
        p) HAS_PROXY=1
           proxy_port=$OPTARG;;
        t) HAS_TINY=1
           tiny_port=$OPTARG;;
        P) PART=$OPTARG;;
        x) setopt xtrace;;
        h|*) usage;;
    esac
done

shift $((OPTIND - 1))
(( $# )) && usage

check_env || exit 1

# Clean the dirs and logs
clear_dirs
rm -fR $LOG $PROXY_LOG $TINY_LOG

# Add a handler to generate a meaningful timeout message
TRAPALRM () {
    echolog "Timeout waiting for $theserver to grab the port reserved for it"
    exec exit 1
}

TRAPTERM () {
    kill_all
    echolog "Driver killed"
    exec exit 2
}
functions[TRAPKILL]=$functions[TRAPTERM]
   
TRAPPIPE () {
    echolog "Broken pipe: a program died unexpectedly."
    exec exit 3
}

case $PART; in
    '') part1
        echolog ""
        part2
        echolog ""
        part3
        echolog ""
        (( final_score = get_score + post_score + conc_score ))
        if (( final_score == MAX_SCORE )); then
            echolog "Received all possible points in Part 1-3, starting bonus part."
            wait_a_bit
            partB
            echolog ""
            (( final_score += bonus_score ))
        fi 
        echoscore "PROXYLAB SCORE" "$final_score/$MAX_SCORE"
        ;;
    l|li|lis|list)
        cat driver/list 2> /dev/null
        ;;
    *)  if typeset -f part$PART > /dev/null; then
            part$PART
            kill_all
        else
            echolog "Incorrect part: $PART"
            usage
        fi
       ;;
esac
