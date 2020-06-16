#!/bin/bash
set -e

# Add local sicbuilder user
# Either use LOCAL_USER_ID:LOCAL_GRP_ID if set via environment
# or fallback to 9001:9001

USER_ID=${LOCAL_USER_ID:-9001}
GRP_ID=${LOCAL_GRP_ID:-9001}

getent group sicbuilder > /dev/null 2>&1 || groupadd -g $GRP_ID sicbuilder
id -u sicbuilder > /dev/null 2>&1 || useradd --shell /bin/bash -u $USER_ID -g $GRP_ID -o -c "" -m sicbuilder

LOCAL_UID=$(id -u sicbuilder)
LOCAL_GID=$(getent group sicbuilder | cut -d ":" -f 3)

if [ ! "$USER_ID" == "$LOCAL_UID" ] || [ ! "$GRP_ID" == "$LOCAL_GID" ]; then
    echo "Warning: User sicbuilder with differing UID $LOCAL_UID/GID $LOCAL_GID already exists, most likely this container was started before with a different UID/GID. Re-create it to change UID/GID."
fi

echo "Starting with UID/GID: $LOCAL_UID:$LOCAL_GID"

export HOME=/home/sicbuilder

# Mount host directories
for dir in .ccache .zcash-params build; do
    if [ -d "/mnt/${dir}" ]; then
        if [ ! -L "/home/sicbuilder/${dir}" ]; then
            ln -sf "/mnt/${dir}" "/home/sicbuilder/${dir}"
        fi
    else
        mkdir -p "/home/sicbuilder/${dir}"
    fi
done

# Fix ownership recursively
chown -RH sicbuilder:sicbuilder /home/sicbuilder

exec /usr/local/bin/gosu sicbuilder "$@"
