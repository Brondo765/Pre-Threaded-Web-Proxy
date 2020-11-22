#!/bin/zsh -f
. ./part-points.sh

grep 'echopart' ./part[0-9A-Z].sh | \
    sed 's/\([0-9B]\):/\1A:/' | sort |\
    sed 's/A:/:/;s/\*\*\*/   /;s/[^"]*"//;s/"//' | \
    while IFS='' read part; do \
        echo -n $part;
        parthead=$(echo "$part" | sed 's/[^A-Z]*\([^:]*\):.*/\1/');
        var=$(sed -n "/$parthead\b/{:a /MAX_/ { s/.*\(MAX_[A-Z_0-9]*\).*/\1/; p; q }; n; ba }" part[0-9A-Z].sh)
        eval "pts=\$$var"
        echo " -- $pts pts"
    done
