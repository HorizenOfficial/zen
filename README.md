Zen 2.0.21-1
==============

What is Horizen?
----------------
A globally accessible and anonymous blockchain for censorship-resistant communications and economic activity.

Upgrading from 2.0.11 source
----------------

To upgrade from any version prior to 2.0.14, you will have to re-clone the repository, the [ZencashOfficial/zen](https://github.com/ZencashOfficial/zen) repository was replaced by a new repository based on Zcash upstream with a different commit history. Merging/pulling is not possible without issues.
Assuming your current repository is stored at `~/zen`, do the following to upgrade:
```{r, engine='bash'}
# if you don't want to keep the old src around
rm -r ~/zen
# or if you do want to keep it
mv ~/zen ~/zen_archived
git clone https://github.com/ZencashOfficial/zen.git
cd ~/zen
```
Now continue with building from source.

Building from source
----------------

1. Get dependencies:
    1. Debian
    ```{r, engine='bash'}
    sudo apt-get install \
          build-essential pkg-config libc6-dev m4 g++-multilib \
          autoconf libtool ncurses-dev unzip git python \
          zlib1g-dev bsdmainutils automake curl
    ```
    2. Centos 7:
    ```{r, engine='bash')
    sudo yum group install 'Development Tools'
    sudo yum install \
    autoconf libtool unzip git python \
    curl automake gcc gcc-c++ patch \
    glibc-static libstdc++-static
    ```
    Please execute the below commands in order.
    ```{r, engine='bash')
    sudo yum install centos-release-scl-rh
    sudo yum install devtoolset-3-gcc devtoolset-3-gcc-c++
    sudo update-alternatives --install /usr/bin/gcc-4.9 gcc-4.9 /opt/rh/devtoolset-3/root/usr/bin/gcc 10
    sudo update-alternatives --install /usr/bin/g++-4.9 g++-4.9 /opt/rh/devtoolset-3/root/usr/bin/g++ 10
    scl enable devtoolset-3 bash
    ```
    3. Windows
    ```{r, engine='bash'}
    sudo apt-get install \
        build-essential pkg-config libc6-dev m4 g++-multilib \
        autoconf libtool ncurses-dev unzip git python \
        zlib1g-dev wget bsdmainutils automake mingw-w64
    ```
    4. Arm
    ```{r, engine='bash'}
    sudo apt-get install \
        build-essential pkg-config libc6-dev m4 g++-multilib-arm-linux-gnueabihf \
        autoconf libtool ncurses-dev unzip git python \
        zlib1g-dev curl bsdmainutils automake cmake cargo
    ```

* Install for Linux
```{r, engine='bash'}
git clone https://github.com/ZencashOfficial/zen.git
cd zen
# Build
./zcutil/build.sh -j$(nproc)
# fetch key
./zcutil/fetch-params.sh
# Run
./src/zend
```

* Install for Mac OS (using clang)

```
Read and follow the README.md at https://github.com/ZencashOfficial/zencash-apple
```

https://github.com/ZencashOfficial/zencash-apple


* Install for Windows (Cross-Compiled, building on Windows is not supported yet)

```
export HOST=x86_64-w64-mingw32
./zcutil/build.sh -j$(nproc)
```

* Install for aarch64(ARM64)

```
mkdir -p ~/bin
cd ~/bin
ln -s /usr/bin/ar aarch64-unknown-linux-gnu-ar
ln -s /usr/bin/g++ aarch64-unknown-linux-gnu-g++
ln -s /usr/bin/gcc aarch64-unknown-linux-gnu-gcc
ln -s /usr/bin/nm aarch64-unknown-linux-gnu-nm
ln -s /usr/bin/ranlib aarch64-unknown-linux-gnu-ranlib
ln -s /usr/bin/strip aarch64-unknown-linux-gnu-strip
PATH=$PATH:~/bin
cd ~/zen/
export HOST=aarch64-unknown-linux
./zcutil/build.sh -j$(nproc)
```
Running Regression Tests
----------------
1. Get dependencies:
    1. Debian
    ```{r, engine='bash'}
    sudo apt-get install \
          python python-dev python-pip python-setuptools \
          python-wheel python-wheel-common python-zmq
    sudo pip install --upgrade pyblake2
    ```
    2. MacOS
    ```{r, engine='bash'}
    brew install python@2
    sudo pip install --upgrade pyblake2 pyzmq
    ```
2. Start test suite
    ```{r, engine='bash'}
    if [[ "$OSTYPE" == "darwin"* ]]; then
      TEST_ARGS="btest gtest sec-hard no-dot-so util-test secp256k1 libsnark univalue rpc --rpc-extended --rpc-exclude=rpcbind_test.py"
    else
      TEST_ARGS="--rpc-extended"
    fi
    export HOST=$(gcc -dumpmachine)
    ./zcutil/fetch-params.sh
    ./qa/zcash/full_test_suite.py ${TEST_ARGS}
    ```
Instructions to redeem pre-block 110,000 ZCL
-------------
1. Linux:
Copy and paste your wallet.dat from ~/.zclassic/ to ~/.zen. That's it!

2. Windows:
Copy and paste your wallet.dat from %APPDATA%/Zclassic/ to %APPDATA%/Zen. That's it!

About
--------------

[Zen](https://horizen.global/) is a platform for secure communications and for deniable economic activity.
Horizen is an evolution of the Zclassic codebase aimed at primarily enabling intriniscally secure communications and
resilient networking.

This software is the Horizen client. It downloads and stores the entire history
of Horizen transactions; depending on the speed of your computer and network
connection, the synchronization process could take a day or more once the
blockchain has reached a significant size.

Security Warnings
-----------------

See important security warnings in
[doc/security-warnings.md](doc/security-warnings.md).

**Horizen is unfinished and highly experimental.** Use at your own risk.

Where do I begin?
-----------------
* The easiest way to get started is to download one of the available GUI wallets from [horizen.global](https://horizen.global)

### Need Help?

* Many guides and tutorials are available at [Horizen Discord](https://discord.gg/CEbKY9w)
  for help and more information.

### Want to participate in development?

* Code review is welcome!

Participation in the Horizen project is subject to a
[Code of Conduct](code_of_conduct.md).

Building
--------

Build Horizen along with most dependencies from source by running
./zcutil/build.sh for Linux.
./zcutil/build-win.sh for Windows
./zcutil/build-mac.sh for MacOS.

License
-------

For license information see the file [COPYING](COPYING).
