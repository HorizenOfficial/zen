#!/bin/bash
## Usage:
##  ./zcutil/build-debian-package.sh

set -e
set -x

BUILD_PATH="/tmp/zcbuild"
# ZEN_MOD_START
PACKAGE_NAME="zen"
# ZEN_MOD_END
SRC_PATH=`pwd`
SRC_DEB=$SRC_PATH/contrib/debian
SRC_DOC=$SRC_PATH/doc

umask 022

if [ ! -d $BUILD_PATH ]; then
    mkdir $BUILD_PATH
fi

# ZEN_MOD_START
PACKAGE_VERSION=$($SRC_PATH/src/zend --version | grep version | cut -d' ' -f4 | tr -d v)
# ZEN_MOD_END
DEBVERSION=$(echo $PACKAGE_VERSION | sed 's/-beta/~beta/' | sed 's/-rc/~rc/' | sed 's/-/+/')
BUILD_DIR="$BUILD_PATH/$PACKAGE_NAME-$PACKAGE_VERSION-amd64"

if [ -d $BUILD_DIR ]; then
    rm -R $BUILD_DIR
fi

DEB_BIN=$BUILD_DIR/usr/bin
DEB_CMP=$BUILD_DIR/usr/share/bash-completion/completions
DEB_DOC=$BUILD_DIR/usr/share/doc/$PACKAGE_NAME
DEB_MAN=$BUILD_DIR/usr/share/man/man1
mkdir -p $BUILD_DIR/DEBIAN $DEB_CMP $DEB_BIN $DEB_DOC $DEB_MAN
chmod 0755 -R $BUILD_DIR/*

# Package maintainer scripts (currently empty)
#cp $SRC_DEB/postinst $BUILD_DIR/DEBIAN
#cp $SRC_DEB/postrm $BUILD_DIR/DEBIAN
#cp $SRC_DEB/preinst $BUILD_DIR/DEBIAN
#cp $SRC_DEB/prerm $BUILD_DIR/DEBIAN
# Copy binaries
# ZEN_MOD_START
cp $SRC_PATH/src/zend $DEB_BIN
cp $SRC_PATH/src/zen-cli $DEB_BIN
cp $SRC_PATH/zcutil/fetch-params.sh $DEB_BIN/zen-fetch-params
# ZEN_MOD_END
# Copy docs
cp $SRC_PATH/doc/release-notes/release-notes-1.0.0.md $DEB_DOC/changelog
cp $SRC_DEB/changelog $DEB_DOC/changelog.Debian
cp $SRC_DEB/copyright $DEB_DOC
cp -r $SRC_DEB/examples $DEB_DOC
# Copy manpages
# ZEN_MOD_START
cp $SRC_DOC/man/zcashd.1 $DEB_MAN/zend.1
cp $SRC_DOC/man/zcash-cli.1 $DEB_MAN/zen-cli.1
cp $SRC_DOC/man/zcash-fetch-params.1 $DEB_MAN/zen-fetch-params.1
# ZEN_MOD_END
# Copy bash completion files
# ZEN_MOD_START
# TODO: keeping these for reference. These might be the valid lines if we keep the zcash build system
# cp $SRC_PATH/contrib/zcashd.bash-completion $DEB_CMP/zend
# cp $SRC_PATH/contrib/zcash-cli.bash-completion $DEB_CMP/zen-cli
cp $SRC_PATH/contrib/bitcoind.bash-completion $DEB_CMP/zend
cp $SRC_PATH/contrib/bitcoin-cli.bash-completion $DEB_CMP/zen-cli
# ZEN_MOD_END
# Gzip files
gzip --best -n $DEB_DOC/changelog
gzip --best -n $DEB_DOC/changelog.Debian

# ZEN_MOD_START
gzip --best -n $DEB_MAN/zend.1
gzip --best -n $DEB_MAN/zen-cli.1
gzip --best -n $DEB_MAN/zen-fetch-params.1
# ZEN_MOD_END

cd $SRC_PATH/contrib

# Create the control file
# ZEN_MOD_START
# TODO: keeping this for reference. These might be the valid lines if we keep the zcash build system
dpkg-shlibdeps $DEB_BIN/zend $DEB_BIN/zen-cli
# dpkg-gencontrol -P$BUILD_DIR -v$DEBVERSION
dpkg-gencontrol -v$PACKAGE_VERSION -P$BUILD_DIR
# ZEN_MOD_END

# Create the Debian package
fakeroot dpkg-deb --build $BUILD_DIR
cp $BUILD_PATH/$PACKAGE_NAME-$PACKAGE_VERSION-amd64.deb $SRC_PATH
# Analyze with Lintian, reporting bugs and policy violations
lintian -i $SRC_PATH/$PACKAGE_NAME-$PACKAGE_VERSION-amd64.deb
exit 0
