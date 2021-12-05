Zen 3.0.3-bitcore
================
What is Horizen?
----------------
Horizen is an inclusive and scalable platform where everyone is empowered and rewarded for their contributions. Horizen’s sidechain platform enables real-world applications mapped onto a public blockchain architecture with the largest node network in the industry. Horizen’s Sidechain-SDK provides developers with all necessary components to deploy their own fully customizable blockchains on Horizen’s open sidechain protocol, Zendoo.

Zend_oo - the Zendoo-powered Sidechain Platform
----------------
Zend_oo is the new version of zend allowing the Horizen mainchain to manage and interact with any number of sidechains. 
This version of zend implements the Zendoo verifiable Cross-Chain Transfer Protocol (CCTP) that allows the creation of ad-hoc sidechains with independently customizable business logic. Additionally, it enables the transfer of coins from the mainchain to sidechains and from sidechains back to the mainchain through SNARK verifiable withdrawal certificates.

Horizen Sidechains are fully scalable and decentralized:
----------------
- The Cross-Chain Transfer Protocol (CCTP) enables coin transferring between sidechains and the mainchain without the need for the mainchain to follow them and to know their internal structure. The coin transfers from sidechains to the mainchain are performed using cryptographically authenticated certificates received from the sidechains.
- Certificate authentication and validation are achieved through the innovative use of SNARK technology, which enables constant-sized proofs of arbitrary computations, without involving a trusted third party and keeps the mainchain totally agnostic from any sidechain rules.

The main feature of our construction is the high degree of generalization. Sidechains are allowed to use their own rules and data, defining custom SNARKs to let the mainchain authenticate their certificates without any knowledge of the sidechain internals.

You can read more about the Zendoo protocol in our [whitepaper](https://www.horizen.io/assets/files/Horizen-Sidechain-Zendoo-A_zk-SNARK-Verifiable-Cross-Chain-Transfer-Protocol.pdf), and explore our default sidechain implementation and SDK in our [Sidechains-SDK](https://github.com/HorizenOfficial/Sidechains-SDK).

New features
----------------
- Sidechain declaration with customizable SNARK verification key
- Forward transfers from mainchain to sidechain
- Sidechain backward transfers from sidechain to mainchain by means of withdrawal certificates
- Sidechain epoch management with liveness monitoring and ceasing procedure
- Mainchain backward transfer request from sidechain to mainchain
- Ceased sidechain withdrawals
- Agnostic withdrawal certificate validation via custom SNARK proof verification (powered by [ginger-lib](https://github.com/HorizenOfficial/ginger-lib))
- Extended RPC interface to create and dispatch sidechain declaration, forward transfer transactions, mainchain backward transfer requests, ceased sidechain withdrawal requests and withdrawal certificates.
- Extensive unit tests and integration tests for verifying sidechain functionality

Building from source
----------------

1. Get dependencies:
    1. Ubuntu
    ```{r, engine='bash'}
    sudo apt-get install \
    build-essential pkg-config libc6-dev m4 g++-multilib \
    autoconf libtool ncurses-dev unzip git python \
    zlib1g-dev bsdmainutils automake curl
    ```
    2. Cross compilation for Windows target
    ```{r, engine='bash'}
    sudo apt-get install \
    build-essential pkg-config libc6-dev m4 g++-multilib \
    autoconf libtool ncurses-dev unzip git python \
    zlib1g-dev wget bsdmainutils automake mingw-w64
    ```

* Install for Linux
```{r, engine='bash'}
git clone https://github.com/HorizenOfficial/zen.git
cd zen
# Build
./zcutil/build.sh -j$(nproc)
# Build for platforms without adx, bmi2 CPU flags
./zcutil/build.sh --legacy-cpu -j$(nproc)
# Build with AddressIndexing support for block explorers
./zcutil/build.sh --enable-address-indexing -j$(nproc)
# Fetch key
./zcutil/fetch-params.sh
# Run
./src/zend
```

* Install for Mac OS (using clang)

Read and follow the README.md at https://github.com/HorizenOfficial/zencash-apple


* Install for Windows (Cross-Compiled, building on Windows is not supported yet)

```
sudo update-alternatives --config x86_64-w64-mingw32-g++
(chose: /usr/bin/x86_64-w64-mingw32-g++-posix)

export HOST=x86_64-w64-mingw32
./zcutil/build.sh -j$(nproc)
```

Running Regression Tests
----------------
1. Get dependencies:
    1. Ubuntu
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

**Zend_oo is experimental and a work in progress.** Use at your own risk.

Need Help?
----------------
Help is available at [Horizen’s Discord](https://www.horizen.io/invite/discord) #zendoo channel.

Want to Participate in Development?
----------------
- Code review is welcomed!
- Please submit any identified issues [here](https://github.com/HorizenOfficial/zen/issues)
- Enroll in the Horizen Early Adopter Program [HEAP](https://heap.horizen.io/) to take part in new product and feature testing

Participation in the Horizen project is subject to a [Code of Conduct](code_of_conduct.md).

License
----------------

For license information see the file [COPYING](COPYING).
