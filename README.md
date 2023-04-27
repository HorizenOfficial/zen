Zend 4.0.0-rc3
================
What is Horizen?
----------------
Horizen is an inclusive and scalable platform where everyone is empowered and rewarded for their contributions. Horizen's sidechain platform enables real-world applications mapped onto a public blockchain architecture with the largest node network in the industry. Horizen's Sidechain-SDK provides developers with all the necessary components to deploy their own fully customizable blockchains on Horizen's open sidechain protocol, Zendoo.

`zend` - the Zendoo-powered node implementation for Horizen ($ZEN).
----------------
`zend` is the official Horizen mainchain client, fully supporting Horizen's sidechains system. 
It implements the Zendoo verifiable Cross-Chain Transfer Protocol (CCTP) that allows the creation of ad-hoc sidechains with independent and customizable business logic.

Horizen Sidechains are fully scalable, secure, and decentralized:
----------------
- The Cross-Chain Transfer Protocol (CCTP) enables coin transfers between the sidechains and the mainchain without knowing their internal structure and without additional software to track them. Those transfers are performed using cryptographically authenticated certificates received from the sidechains.
- Certificate authentication and validation are achieved through the innovative use of zk-SNARKs.
Thanks to this technology, the resulting zero-knowledge proofs have a constant and succinct size. 
Horizen can verify proofs of arbitrary computations and can remain agnostic of the sidechains' use-cases and rules.
- This technology makes Horizen's sidechain system trustless.

You can read more about the Zendoo protocol in our [whitepaper](https://www.horizen.io/assets/files/Horizen-Sidechain-Zendoo-A_zk-SNARK-Verifiable-Cross-Chain-Transfer-Protocol.pdf), and explore a reference sidechain implementation in our [Sidechains-SDK](https://github.com/HorizenOfficial/Sidechains-SDK).

Top features
----------------
INTERACTION 
- RPC interface for general access to data and all available functions


SECURITY
- Proof-of-Work, the most secure consensus, ensured by one of the largest node networks in the industry
- Base58 address check, protecting from money getting lost in case of typos
- 51% attack protection
- Replay attack protection
- Sidechains state provability, powered by zk-SNARKs
- TLS end-to-end encryption - Available for Secure and Super nodes

PRIVACY 
- Shielded transactions, powered by zk-SNARKs
- Privacy-preserving sidechains supported out-of-the-box, powered by zk-SNARKs

SCALABILITY 
- Extended RPC interface to create and dispatch sidechain declaration, forward transfers, mainchain backward transfer requests, ceased sidechain withdrawals, and certificates.
- Sidechain declaration with customizable zk-SNARK verification key
- Sidechain epoch management with liveness monitoring and ceasing procedure
- Mainchain backward transfer request support for sidechains, to collect Zen back on a mainchain address
- Agnostic certificate validation via custom zk-SNARK proof verification (powered by [ginger-lib](https://github.com/HorizenOfficial/ginger-lib))
- Secure sidechain validator's key rotation support via custom zk-SNARK proof verification
- Support for variable epoch lengths (enabling non-ceasable sidechains) 

Compatibility
----------------
`zend` only supports 64-bit little-endian systems, it cannot be compiled on different architectures.

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
    For a detailed list of dependencies, please refer to the [dependencies section](doc/dependencies.md).

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
    pip install --upgrade pyblake2 websocket-client requests
    ```

    2. MacOS
    ```{r, engine='bash'}
    brew install python@2
    pip install --upgrade pyblake2 pyzmq websocket-client requests
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

**`zend` is experimental and a work in progress.** Use at your own risk.

Need Help?
----------------
Help is available at [Horizen's Discord](https://www.horizen.io/invite/discord) #open-a-ticket channel.

Want to Participate in Development?
----------------
- Code review is welcomed!
- Please submit any identified issues [here](https://github.com/HorizenOfficial/zen/issues)
- Enroll in the Horizen Early Adopter Program [HEAP](https://heap.horizen.io/) to take part in new product and feature testing

Participation in the Horizen project is subject to a [Code of Conduct](code_of_conduct.md).

License
----------------

For license information see the file [COPYING](COPYING).
