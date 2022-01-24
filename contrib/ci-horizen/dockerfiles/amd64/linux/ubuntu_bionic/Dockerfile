FROM ubuntu:bionic

MAINTAINER cronic@zensystem.io

COPY entrypoint.sh /usr/local/bin/entrypoint.sh

SHELL ["/bin/bash", "-c"]

ENV LC_ALL=C.UTF-8 LANG=C.UTF-8 LANGUAGE=C.UTF-8

RUN set -euxo pipefail \
    && export DEBIAN_FRONTEND=noninteractive \
    && apt-get update \
    && apt-get -y --no-install-recommends install apt-utils \
    && apt-get -y --no-install-recommends dist-upgrade \
    && apt-get -y --no-install-recommends install aria2 autoconf automake bsdmainutils build-essential ca-certificates \
       cmake curl dirmngr fakeroot git g++-multilib gnupg2 help2man libc6-dev libgomp1 libtool lintian m4 ncurses-dev \
       pigz pkg-config pv python3-dev python3-pip python3-setuptools python3-wheel time unzip wget zlib1g-dev \
    && pip3 install b2 pyblake2 websocket-client2 pyzmq \
    && BASEURL="https://github.com/tianon/gosu/releases/download/" \
    && GOSU_VERSION="1.13" \
    && DPKG_ARCH="$(dpkg --print-architecture | awk -F- '{ print $NF }')" \
    && curl -o /usr/local/bin/gosu -SL "${BASEURL}/${GOSU_VERSION}/gosu-${DPKG_ARCH}" \
    && curl -o /usr/local/bin/gosu.asc -SL "${BASEURL}/${GOSU_VERSION}/gosu-${DPKG_ARCH}.asc" \
    && export GNUPGHOME="$(mktemp -d)" \
    && ( gpg2 --batch --keyserver hkps://keys.openpgp.org --recv-keys B42F6819007F00F88E364FD4036A9C25BF357DD4 || \
       gpg2 --batch --keyserver hkps://keyserver.ubuntu.com:443 --recv-keys B42F6819007F00F88E364FD4036A9C25BF357DD4 || \
       gpg2 --batch --keyserver hkp://p80.pool.sks-keyservers.net:80 --recv-keys B42F6819007F00F88E364FD4036A9C25BF357DD4 || \
       gpg2 --batch --keyserver hkp://ha.pool.sks-keyservers.net --recv-keys B42F6819007F00F88E364FD4036A9C25BF357DD4 || \
       gpg2 --batch --keyserver pgp.mit.edu --recv-key B42F6819007F00F88E364FD4036A9C25BF357DD4 || \
       gpg2 --batch --keyserver keyserver.pgp.com --recv-keys B42F6819007F00F88E364FD4036A9C25BF357DD4 || \
       gpg2 --batch --keyserver pgp.key-server.io --recv-keys B42F6819007F00F88E364FD4036A9C25BF357DD4 ) \
    && gpg2 --batch --verify /usr/local/bin/gosu.asc /usr/local/bin/gosu \
    && ( gpgconf --kill dirmngr || true ) \
    && ( gpgconf --kill gpg-agent || true ) \
    && rm -rf "$GNUPGHOME" /usr/local/bin/gosu.asc \
    && unset GNUPGHOME \
    && chmod +x /usr/local/bin/gosu \
    && gosu nobody true \
    && BASEURL=$(curl -s https://api.github.com/repos/ipfs/go-ipfs/releases | grep browser_download_url | grep -v 'rc[0-9]/go' | awk 'FNR <= 1' | cut -d '"' -f 4 | sed 's:/[^/]*$::') \
    && IPFS_VERSION=$(echo -n $BASEURL | sed 's:.*/::') \
    && TMP="$(mktemp -d)" \
    && curl -SLo "$TMP/go-ipfs_${IPFS_VERSION}_linux-${DPKG_ARCH}.tar.gz" "${BASEURL}/go-ipfs_${IPFS_VERSION}_linux-${DPKG_ARCH}.tar.gz" \
    && curl -SLo "$TMP/go-ipfs_${IPFS_VERSION}_linux-${DPKG_ARCH}.tar.gz.sha512" "${BASEURL}/go-ipfs_${IPFS_VERSION}_linux-${DPKG_ARCH}.tar.gz.sha512" \
    && cd $TMP && sha512sum -c "go-ipfs_${IPFS_VERSION}_linux-${DPKG_ARCH}.tar.gz.sha512" && tar -xf "go-ipfs_${IPFS_VERSION}_linux-${DPKG_ARCH}.tar.gz" \
    && cd go-ipfs && ./install.sh && cd && rm -rf $TMP \
    && apt-get -y autoremove --purge \
    && apt-get -y autoclean \
    && apt-get clean \
    && rm -rf /var/cache/apt/archives/*.deb /var/lib/apt/lists/* /root/.cache /tmp/* \
    && chmod +x /usr/local/bin/entrypoint.sh

VOLUME ["/mnt/.ccache"]

VOLUME ["/mnt/.zcash-params"]

VOLUME ["/mnt/build"]

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
