#/usr/bin/bash

# check if we have an id passed in $1
if [ -z "$1" ]; then
    echo "Select a window to share."
    WID=$(xwininfo -int | grep "Window id:" | awk '{ print $4 }')
    echo $WID
else
    # check if $1 == "current"
    if [ "$1" = "current" ]; then
        WID=$(xdotool getactivewindow)
    else
        WID=$1
    fi
fi

node ./server/server.js $WID
