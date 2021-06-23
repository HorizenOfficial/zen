Zen 2.1.0-beta4 <!---TO BE BUMPED UP UPON RELEASE -->
================
What is Horizen?
----------------
Horizen is an inclusive and scalable platform where everyone is empowered and rewarded for their contributions. Horizen’s sidechain platform enables real-world applications mapped onto a public blockchain architecture with the largest node network in the industry. Horizen’s Sidechain-SDK provides developers with all necessary components to deploy their own fully customizable blockchains on Horizen’s open sidechain protocol, Zendoo.

Zend_oo - the Zendoo-powered Sidechain Platform
----------------
Zend_oo is the beta version of zend allowing the Horizen mainchain to manage and interact with any number of sidechains. It downloads and stores the entire history of Horizen transactions. Depending on the speed of your computer and network connection, the synchronization process could take a day or more.

This version of zend implements the Zendoo verifiable Cross-Chain Transfer Protocol (CCTP) that allows the creation of ad-hoc sidechains with independently customizable business logic. Additionally, it enables the transfer of coins from the mainchain to sidechains and from sidechains back to the mainchain through verifiable withdrawal certificates.

Horizen Sidechains are fully decentralized:
----------------
- Sidechain nodes observe the mainchain but the mainchain only observes cryptographically authenticated certificates received from the sidechain.
- Certificate authentication and validation are achieved through the innovative use of SNARK technology, which enables constant-sized proofs of arbitrary computations, without involving a trusted third party and keeps the mainchain totally agnostic from any sidechain rules.

The main feature of our construction is the high degree of generalization. Sidechains are allowed to use their own rules and data, defining custom SNARKs to let the mainchain authenticate their certificates without any knowledge of the sidechain internals.

You can read more about the Zendoo protocol in our [whitepaper](https://www.horizen.global/assets/files/Horizen-Sidechain-Zendoo-A_zk-SNARK-Verifiable-Cross-Chain-Transfer-Protocol.pdf), and explore our default sidechain implementation and SDK in our [Sidechains-SDK](https://github.com/HorizenOfficial/Sidechains-SDK).

Beta features
----------------
- Sidechain declaration with customizable SNARK verification key
- Forward transfer transaction to sidechain
- Sidechain epoch management with liveness monitoring and ceasing procedure
- Sidechain backward transfer by means of withdrawal certificate
- Agnostic withdrawal certificate validation via custom SNARK proof verification (powered by [zendoo-mc-cryptolib](https://github.com/HorizenOfficial/zendoo-mc-cryptolib))
- Extended RPC interface to create and dispatch sidechain declaration, forward transfer transactions, and withdrawal certificates
- Extensive unit tests and integrations tests for verifying sidechain functionality
- Public sidechain testnet is separate from the normal testnet
- Graphical wallet allowing easy sidechain creation, forward transfers to sidechains, list of existing sidechains and more: [Sphere by Horizen](https://github.com/HorizenOfficial/Sphere_by_Horizen_Sidechain_Testnet/releases/latest).

Branching strategy
----------------
- [sidechains_testnet](https://github.com/HorizenOfficial/zend_oo/tree/sidechains_testnet) - Sidechain testnet branch is the release branch of sidechain testnet beta versions. Compile from this branch to run a mainchain node on the sidechain test network.
- [sidechains_dev](https://github.com/HorizenOfficial/zend_oo/tree/sidechains_dev) - Sidechain development branch. Ongoing development takes place here. Any time a release is being done this branch will be merged into [sidechains_testnet](https://github.com/HorizenOfficial/zend_oo/tree/sidechains_testnet).

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
git clone https://github.com/HorizenOfficial/zend_oo.git
cd zend_oo
# Build
./zcutil/build.sh -j$(nproc)
# fetch key
./zcutil/fetch-params.sh
# Run
./src/zend
```

* Install for Mac OS (using clang)

```
Read and follow the README.md at https://github.com/HorizenOfficial/zencash-apple
```

https://github.com/HorizenOfficial/zencash-apple


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
    sudo pip install --upgrade pyblake2 websocket-client
    ```
    2. MacOS
    ```{r, engine='bash'}
    brew install python@2
    sudo pip install --upgrade pyblake2 pyzmq websocket-client
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
    
Security Warnings
----------------

See important security warnings in [doc/security-warnings.md](doc/security-warnings.md).

**Horizen is unfinished and highly experimental.** Use at your own risk.

Where do I begin?
----------------
The easiest way to get started is to download our flagship app [Sphere by Horizen](https://github.com/HorizenOfficial/Sphere_by_Horizen_Sidechain_Testnet/releases/latest) which is the entry point of most Horizen services.

Need Help?
----------------
Help is available at [Horizen’s Discord](https://www.horizen.global/invite/discord) #sidechains channel.

Want to Participate in Development?
----------------
- Code review is welcomed!
- Please submit any identified issues [here](https://github.com/HorizenOfficial/zend_oo/issues)
- Enroll in the Horizen Early Adopter Program [HEAP](https://heap.horizen.global/) to take part in new product and feature testing

Participation in the Horizen project is subject to a [Code of Conduct](code_of_conduct.md).

License
----------------

For license information see the file [COPYING](COPYING).
