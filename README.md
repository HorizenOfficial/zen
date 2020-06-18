Zen 2.1.0-beta3
==============

What is Horizen?
----------------
A globally accessible and anonymous blockchain for censorship-resistant communications and economic activity.

Zend_oo - the Zendoo-powered Sidechain Platform
-------------------
**Zend_oo** is the beta version of Zend allowing Horizen Mainchain to manage and interact with any number of **sidechains**.

This version of Zend implements the **Zendoo** verifiable Cross-Chain Transfer Protocol that allows the creation of ad-hoc sidechains, each with an independent **custom** business logic. Moreover it grants the possibility of transferring coins from the Mainchain and allows to safely retrieve coins back from the sidechain by means of verifiable withdrawal certificates.

Horizen Sidechains are **fully decentralized**: sidechain nodes observe the Mainchain but the Mainchain only observes cryptographically authenticated certificates received from the sidechain; Certificate authentication and validation are achieved thanks to an innovative use of the **SNARK** technology, which enable constant-sized proofs of arbitrary computations, not involving any trusted third party and keeping Mainchain totally **agnostic** from sidechain's rules.

The main feature of our construction is the high degree of **generalization**: sidechains are allowed to use their own rules and data, defining custom SNARKs to let mainchain authenticate their certificates without any knowledge of the sidechain internals.

You can read more about the Zendoo protocol in our [whitepaper](https://www.horizen.global/assets/files/Horizen-Sidechain-Zendoo-A_zk-SNARK-Verifiable-Cross-Chain-Transfer-Protocol.pdf), and explore our default sidechain implementation and SDK in our [Sidechains-SDK](https://github.com/ZencashOfficial/Sidechains-SDK).

### **Beta Preview features**
-------------------

- Sidechain Declaration with customizable SNARK verification key;
- Forward Transfer transaction to sidechain;
- Sidechain epoch management with liveness monitoring and ceasing procedure;
- Sidechain Backward Transfer by means of Withdrawal Certificate;
- Agnostic Withdrawal Certificate validation via custom SNARK proof verification (powered by[ zendoo-mc-cryptolib](https://github.com/ZencashOfficial/zendoo-mc-cryptolib));
- Extended rpc interface to create and dispatch Sidechain Declaration, Forward Transfer transactions and Withdrawal Certificates;
- Extensive unit tests and integrations tests for verifying sidechain functionality;
- Public Sidechain testnet separate from normal testnet3.

Branching strategy
----------------
- [sidechains_testnet](https://github.com/ZencashOfficial/zend_oo/tree/sidechains_testnet) - Sidechains testnet branch, release branch of Sidechains testnet beta versions, compile from this branch to run a Mainchain node on the Sidechains test network
- [sidechains_dev](https://github.com/ZencashOfficial/zend_oo/tree/sidechains_dev) - Sidechains development branch, ongoing development takes place here, any time a release is being done this branch will be merged into [sidechains_testnet](https://github.com/ZencashOfficial/zend_oo/tree/sidechains_testnet)

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
git clone https://github.com/ZencashOfficial/zend_oo.git
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

License
-------

For license information see the file [COPYING](COPYING).
