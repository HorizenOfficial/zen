Changelog
=========

cronicc (21):
      Update about section in README.md
      Change user agent in help messages from 'MagicBean' to 'zen'
      Add websocket-client python test dependency
      Update CI builder dockerfiles:
      Replace xenial with focal builder in travis
      Use focal host os and x-large vm size in travis linux build/test stages
      Add some buildsystem information to entrypoint.sh
      More Test stage parallelization
      Add more fallback gpg2 servers for key fetch to Dockerfiles
      Always login to hub.docker.com with read only account, even when only pulling
      Change interpreter to python2
      Update python interpreter workaround for MacOS tests
      Set version 2.0.24
      Add checkpoint blocks
      Update version in readme
      Update Debian package info
      Update OpenSSL to 1.1.1l
      Set deprecation height 1047600/2021-12-1
      Update manpages
      Update changelog
      Pull in patch fixing OpenSSL build error on older MacOS: https://github.com/openssl/openssl/commit/96ac8f13f4d0ee96baf5724d9f96c44c34b8606c

luisantoniocrag (12):
      Fix RPC misc call help messages
      Fix RPC blockchain call help messages
      Fix RPC mining call help messages
      Fix RPC net call help messages
      Fix RPC rawtransaction call help messages
      Fix RPC rpcdisclosure call help messages
      Fix RPC server call help messages
      Fix RPC rpcdump call help messages
      Fix RPC rpcwallet call help messages
      Fix misc rpc help messages
      Fix rpcwallet
      Update src/rpc/blockchain.cpp

