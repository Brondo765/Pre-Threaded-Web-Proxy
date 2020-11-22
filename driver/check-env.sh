#!/bin/zsh -f

check_env_curl () {
    case $(curl --version 2> /dev/null | cut -d' ' -f2); in
        7.[7-9][0-9]*|[89]*|1[0-9]*) ;;
        *) echo "Error: curl version at least 7.72 required."
           exit 7
           ;;
    esac
}

check_env_tiny () {
    # Make sure we have a Tiny directory
    if [ ! -d ./tiny ]; then 
        echo "Error: ./tiny directory not found."
        exit 1
    fi

    # If there is no Tiny executable, then try to build it
    if [ ! -x ./tiny/tiny ]; then 
        echo "Building the tiny executable."
        make -C lib || exit 2
        make -C tiny || exit 2
        echo ""
    fi

    # Make sure we have all the Tiny files we need
    if [ ! -x ./tiny/tiny ]; then 
        echo "Error: ./tiny/tiny not found or not an executable file."
        exit 3
    fi
}

wait_a_bit () {
    echo -n "Type Ctrl-C to kill the driver... "
    for i in {5..1}; do
        echo -n "$i "
        sleep 1
    done
    echo ""
}

check_agreement () {
    if ! [[ -e driver/.agreement-accepted ]]; then
        cat <<EOF
                 =============================================
                           ACADEMIC INTEGRITY NOTICE
                               appears only once
                 =============================================

This is an  individual assignment.  Your submission will be  compared to that of
all your peers and with publicly available submissions using dedicated software.
You are strictly  prohibited from using any  source other than the  text and the
lectures  when  working  on this  lab,  except  for  the  bonus Poll  part.   In
particular, you  are strictly  forbidden from  acquiring hints  and/or solutions
from the  internet or  from any  other external resource  or person  besides the
instructor.   Copying is  strictly forbidden.   Disciplinary actions  range from
grade penalty to expulsion.

EOF
        echo -n "Do you agree with these rules (type y or n)? "
        if read -q; then
            touch driver/.agreement-accepted
            echo ""
        else
            echo ""
            echo "Can't start the driver without accepting rules."
            exit 4
        fi
    fi
}

check_env () {
    check_env_curl
    check_agreement
    check_env_tiny
    
    # Make sure we have an existing executable proxy
    if [ ! -x src/proxy ]; then 
        echo "Error: src/proxy not found or not an executable file. Please run make and try again."
        exit 4
    fi

    # Make sure we have an existing executable nop-server.py file
    if [ ! -x driver/nop-server.py ]; then 
        echo "Error: ./nop-server.py not found or not an executable file."
        exit 5
    fi

    if ( cd src/; make --recon | grep -q gcc ); then
        echo "WARNING: the proxy sources are more recent than the executable.  Perhaps run make?"
        wait_a_bit
    fi
}
