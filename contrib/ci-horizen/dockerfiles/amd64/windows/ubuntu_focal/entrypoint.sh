#!/bin/bash
set -e

# Add local zenbuilder user
# Either use LOCAL_USER_ID:LOCAL_GRP_ID if set via environment
# or fallback to 9001:9001

USER_ID=${LOCAL_USER_ID:-9001}
GRP_ID=${LOCAL_GRP_ID:-9001}

getent group zenbuilder > /dev/null 2>&1 || groupadd -g "$GRP_ID" zenbuilder
id -u zenbuilder > /dev/null 2>&1 || useradd --shell /bin/bash -u "$USER_ID" -g "$GRP_ID" -o -c "" -m zenbuilder

LOCAL_UID=$(id -u zenbuilder)
LOCAL_GID=$(getent group zenbuilder | cut -d ":" -f 3)

if [ ! "$USER_ID" == "$LOCAL_UID" ] || [ ! "$GRP_ID" == "$LOCAL_GID" ]; then
    echo "Warning: User zenbuilder with differing UID $LOCAL_UID/GID $LOCAL_GID already exists, most likely this container was started before with a different UID/GID. Re-create it to change UID/GID."
fi

export HOME=/home/zenbuilder

gcc -v
echo
lscpu
echo
free -h
echo
echo "Username: zenbuilder, HOME: $HOME, UID: $LOCAL_UID, GID: $LOCAL_GID"

# Mount host directories
for dir in .ccache .zcash-params build; do
    if [ -d "/mnt/${dir}" ]; then
        if [ ! -L "/home/zenbuilder/${dir}" ]; then
            ln -sf "/mnt/${dir}" "/home/zenbuilder/${dir}"
        fi
    else
        mkdir -p "/home/zenbuilder/${dir}"
    fi
done

# Fix ownership recursively
chown -RH zenbuilder:zenbuilder /home/zenbuilder

exec /usr/local/bin/gosu zenbuilder "$@"
