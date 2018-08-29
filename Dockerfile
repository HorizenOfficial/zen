# Use Fedora 28 docker image
# Multistage docker build, requires docker 17.05
FROM fedora:28 as builder

# If you have an old version of the docker, then
# correct the previous line, it should be the
# FROM fedora

RUN dnf -y update && dnf -y install make  gcc-c++ cmake git wget libzip bzip2 which openssl-devel && dnf -y install \
git pkgconfig automake autoconf ncurses-devel python \
python-zmq wget curl gtest-devel gcc gcc-c++ libtool patch

WORKDIR /app

## Boost
#ARG BOOST_VERSION=1_67_0
#ARG BOOST_VERSION_DOT=1.67.0
#ARG BOOST_HASH=2684c972994ee57fc5632e03bf044746f6eb45d4920c343937a465fd67a5adba

#RUN set -ex \
#    && curl -s -L -o  boost_${BOOST_VERSION}.tar.bz2 https://dl.bintray.com/boostorg/release/${BOOST_VERSION_DOT}/source/boost_${BOOST_VERSION}.tar.bz2 \
#        && echo "${BOOST_HASH} boost_${BOOST_VERSION}.tar.bz2" | sha256sum -c \
#            && tar -xvf boost_${BOOST_VERSION}.tar.bz2 \
#           && mv boost_${BOOST_VERSION} boost \
#             && cd boost \
#               && ./bootstrap.sh \
#                            && ./b2 --build-type=minimal link=static -j4 runtime-link=static --with-chrono --with-date_time --with-filesystem --with-program_options --with-regex --with-serialization --with-system --with-thread --stagedir=stage threading=multi threadapi=pthread cflags="-fPIC" cxxflags="-fPIC" stage

#COPY . /app

RUN dnf install -y git glibc-static libstdc++-static && cd / && git clone --recursive https://github.com/ZencashOfficial/zen.git

RUN cd /zen && ./zcutil/build.sh -j 1

#RUN cd /zen && ./zcutil/fetch-params.sh

FROM fedora:28 as builder2

#RUN dnf -y update && dnf install wget libstdc++ libgomp -y && dnf clean all

COPY --from=builder /zen/src/* /usr/local/bin/
COPY --from=builder /zen/zcutil/* /usr/local/bin/

FROM fedora:28

RUN dnf -y update && dnf install wget libstdc++ libgomp iputils -y && dnf clean all

COPY --from=builder2 /usr/local/bin/libzcashconsensus.so* \
                     /usr/local/bin/zen-cli \
                     /usr/local/bin/zen-gtest \
                     /usr/local/bin/zen-tx \
                     /usr/local/bin/zend \
                     /usr/local/bin/

RUN fetch-params.sh && zend --version || /bin/true
