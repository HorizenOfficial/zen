# Copyright (c) 2014 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.


#
# Helpful routines for regression testing
#

# Add python-bitcoinrpc to module search path:
import os
import sys

from binascii import hexlify, unhexlify
from base64 import b64encode
from decimal import Decimal, ROUND_DOWN
import codecs
import json
import random
import shutil
import subprocess
import time
import re
import codecs
from test_framework.authproxy import AuthServiceProxy, JSONRPCException

COIN = 100000000 # 1 zec in zatoshis

def p2p_port(n):
    return 11000 + n + os.getpid()%999
def rpc_port(n):
    return 12000 + n + os.getpid()%999
def ws_port(n):
    return 7474 + n + os.getpid()%999

def get_ws_url(extra_args, i):
    ws_url=None
    wsport_arg=None

    if extra_args is not None and '-websocket=1' in extra_args:
        wsp = 0
        for s in extra_args:
            if '-wsport=' in s:
                for d in s.split('='):
                    if d.isdigit():
                        wsp = int(d)
                        break
            # if more than one wsport option is set, the last wins
            #if wsp != 0:
            #    break
        if wsp == 0:
            # if ws port has not been set in args, we set it
            wsp = ws_port(i)
            wsport_arg = '-wsport=%d' % wsp

        ws_url = "ws://localhost:%d" % wsp
        print("######### WEBSOCKET URL: ", ws_url)

    return ws_url, wsport_arg


def check_json_precision():
    """Make sure json library being used does not lose precision converting BTC values"""
    n = Decimal("20000000.00000003")
    satoshis = int(json.loads(json.dumps(float(n)))*1.0e8)
    if satoshis != 2000000000000003:
        raise RuntimeError("JSON encode/decode loses precision")

def bytes_to_hex_str(byte_str):
    return hexlify(byte_str).decode('ascii')

def hex_str_to_bytes(hex_str):
    return unhexlify(hex_str.encode('ascii'))

def str_to_hex_str(str):
    return hexlify(str.encode('ascii')).decode('ascii')

def hex_str_to_str(hex_str):
    return unhexlify(hex_str.encode('ascii')).decode('ascii')

def str_to_b64str(string):
    return b64encode(string.encode('utf-8')).decode('ascii')

def swap_bytes(input_buf):
    return codecs.encode(codecs.decode(input_buf, 'hex')[::-1], 'hex').decode()

def sync_blocks(rpc_connections, wait=1, p=False, limit_loop=0):
    """
    Wait until everybody has the same block count or a limit has been exceeded
    """
    loop_num = 0
    while True:
        if limit_loop > 0:
            loop_num += 1
            if loop_num > limit_loop:
                break
        counts = [ x.getblockcount() for x in rpc_connections ]
        if p :
            print(counts)
        if counts == [ counts[0] ]*len(counts):
            break
        time.sleep(wait)

def sync_mempools(rpc_connections, wait=1):
    """
    Wait until everybody has the same transactions in their memory
    pools, and has notified all internal listeners of them
    """
    while True:
        pool = set(rpc_connections[0].getrawmempool())
        num_match = 1
        for i in range(1, len(rpc_connections)):
            if set(rpc_connections[i].getrawmempool()) == pool:
                num_match = num_match+1
        if num_match == len(rpc_connections):
            break
        time.sleep(wait)

    # Now that the mempools are in sync, wait for the internal
    # notifications to finish
    while True:
        notified = [ x.getmempoolinfo()['fullyNotified'] for x in rpc_connections ]
        if notified == [ True ] * len(notified):
            break
        time.sleep(wait)


bitcoind_processes = {}

'''
Known debug trace categories:
    "addrman", "alert", "amqp", "bench", "db", "estimatefee", "forks", "forks_2", "http", 
    "mempool", "net", "partitioncheck", "paymentdisclosure", "pow", "prune", "py",
    "reindex", "rpc", "selectcoin", "status","tor", "zmq", 
'''
def initialize_datadir(dirname, n):
    datadir = os.path.join(dirname, "node"+str(n))
    if not os.path.isdir(datadir):
        os.makedirs(datadir)
    with open(os.path.join(datadir, "zen.conf"), 'w') as f:
        f.write("regtest=1\n");
        f.write("showmetrics=0\n");
        f.write("rpcuser=rt\n");
        f.write("rpcpassword=rt\n");
        f.write("port="+str(p2p_port(n))+"\n");
        f.write("rpcport="+str(rpc_port(n))+"\n");
        f.write("listenonion=0\n");
#        f.write("debug=net\n");
#        f.write("logtimemicros=1\n");
    return datadir

def rpc_url(i, rpchost=None):
    host = '127.0.0.1'
    port = rpc_port(i)
    if rpchost:
        parts = rpchost.split(':')
        if len(parts) == 2:
            host, port = parts
        else:
            host = rpchost
    return "http://rt:rt@%s:%d" % (host, int(port))

def initialize_chain(test_dir):
    """
    Create (or copy from cache) a 200-block-long chain and
    4 wallets.
    zend and zen-cli must be in search path.
    """
    if os.path.isdir(os.path.join("cache", "node0")):
        if os.stat("cache").st_mtime + (60 * 60) < time.time():
            print("initialize_chain(): Removing stale cache")
            shutil.rmtree("cache")
            
    if not os.path.isdir(os.path.join("cache", "node0")):
        devnull = open("/dev/null", "w+")
        # Create cache directories, run bitcoinds:
        for i in range(4):
            datadir=initialize_datadir("cache", i)
            args = [ os.getenv("BITCOIND", "zend"), "-keypool=1", "-datadir="+datadir, "-discover=0", "-rpcservertimeout=600" ]
            if i > 0:
                args.append("-connect=127.0.0.1:"+str(p2p_port(0)))
            bitcoind_processes[i] = subprocess.Popen(args)
            if os.getenv("PYTHON_DEBUG", ""):
                print("initialize_chain: zend started, calling zen-cli -rpcwait getblockcount")
            subprocess.check_call([ os.getenv("BITCOINCLI", "zen-cli"), "-datadir="+datadir,
                                    "-rpcwait", "getblockcount"], stdout=devnull)
            if os.getenv("PYTHON_DEBUG", ""):
                print("initialize_chain: zen-cli -rpcwait getblockcount completed")
        devnull.close()
        rpcs = []
        for i in range(4):
            try:
                url = "http://rt:rt@127.0.0.1:%d"%(rpc_port(i),)
                rpcs.append(AuthServiceProxy(url))
            except:
                sys.stderr.write("Error connecting to "+url+"\n")
                sys.exit(1)

        # Create a 200-block-long chain; each of the 4 nodes
        # gets 25 mature blocks and 25 immature.
        # blocks are created with timestamps 10 minutes apart, starting
        # at Fri, 12 May 2017 00:15:50 GMT (genesis block time)
        # block_time = 1494548150
        block_time = int(time.time()) - (200 * 150) # 200 is number of blocks and 150 is blocktime target spacing 150 / 60 = 2.5 min
        for i in range(2):
            for peer in range(4):
                for j in range(25):
                    set_node_times(rpcs, block_time)
                    rpcs[peer].generate(1)
                    # block_time += 10*60 # this was BTC target spacing ??
                    block_time += 150   # ZEN blocktime target spacing
                # Must sync before next peer starts generating blocks
                sync_blocks(rpcs)
        # Check that local time isn't going backwards                
        assert_greater_than(time.time() + 1, block_time)
                
        # Shut them down, and clean up cache directories:
        stop_nodes(rpcs)
        wait_bitcoinds()
        for i in range(4):
            os.remove(log_filename("cache", i, "debug.log"))
            os.remove(log_filename("cache", i, "db.log"))
            os.remove(log_filename("cache", i, "peers.dat"))
            os.remove(log_filename("cache", i, "fee_estimates.dat"))

    for i in range(4):
        from_dir = os.path.join("cache", "node"+str(i))
        to_dir = os.path.join(test_dir,  "node"+str(i))
        shutil.copytree(from_dir, to_dir)
        initialize_datadir(test_dir, i) # Overwrite port/rpcport in zen.conf

def initialize_chain_clean(test_dir, num_nodes):
    """
    Create an empty blockchain and num_nodes wallets.
    Useful if a test case wants complete control over initialization.
    """
    for i in range(num_nodes):
        initialize_datadir(test_dir, i)


def _rpchost_to_args(rpchost):
    '''Convert optional IP:port spec to rpcconnect/rpcport args'''
    if rpchost is None:
        return []

    match = re.match('(\[[0-9a-fA-f:]+\]|[^:]+)(?::([0-9]+))?$', rpchost)
    if not match:
        raise ValueError('Invalid RPC host spec ' + rpchost)

    rpcconnect = match.group(1)
    rpcport = match.group(2)

    if rpcconnect.startswith('['): # remove IPv6 [...] wrapping
        rpcconnect = rpcconnect[1:-1]

    rv = ['-rpcconnect=' + rpcconnect]
    if rpcport:
        rv += ['-rpcport=' + rpcport]
    return rv

def start_node(i, dirname, extra_args=None, rpchost=None, timewait=None, binary=None):
    """
    Start a zend and return RPC connection to it
    """
    datadir = os.path.join(dirname, "node"+str(i))
    if binary is None:
        binary = os.getenv("BITCOIND", "zend")
    args = [ binary, "-datadir="+datadir, "-keypool=1", "-discover=0", "-rest", "-rpcservertimeout=600" ]

    ws_url, wsport_arg = get_ws_url(extra_args, i)
    if wsport_arg is not None: args.extend([wsport_arg])
    if extra_args is not None: args.extend(extra_args)
    bitcoind_processes[i] = subprocess.Popen(args)
    devnull = open("/dev/null", "w+")
    if os.getenv("PYTHON_DEBUG", ""):
        print("start_node: zend started, calling zen-cli -rpcwait getblockcount")
    subprocess.check_call([ os.getenv("BITCOINCLI", "zen-cli"), "-datadir="+datadir] +
                          _rpchost_to_args(rpchost)  +
                          ["-rpcwait", "getblockcount"], stdout=devnull)
    if os.getenv("PYTHON_DEBUG", ""):
        print("start_node: calling zen-cli -rpcwait getblockcount returned")
    devnull.close()
    url = rpc_url(i, rpchost)
    if timewait is not None:
        proxy = AuthServiceProxy(url, ws_url=ws_url, timeout=timewait)
    else:
        proxy = AuthServiceProxy(url, ws_url=ws_url)
    proxy.url = url # store URL on proxy for info
    proxy.ws_url = ws_url # store URL on proxy for info
    return proxy

def start_nodes(num_nodes, dirname, extra_args=None, rpchost=None, binary=None):
    """
    Start multiple zends, return RPC connections to them
    """
    if extra_args is None: extra_args = [ None for i in range(num_nodes) ]
    if binary is None: binary = [ None for i in range(num_nodes) ]
    return [ start_node(i, dirname, extra_args[i], rpchost, binary=binary[i]) for i in range(num_nodes) ]

def log_filename(dirname, n_node, logname):
    return os.path.join(dirname, "node"+str(n_node), "regtest", logname)

def check_node(i):
    bitcoind_processes[i].poll()
    return bitcoind_processes[i].returncode

def stop_node(node, i):
    node.stop()
    bitcoind_processes[i].wait()
    del bitcoind_processes[i]

def stop_nodes(nodes):
    for node in nodes:
        node.stop()
    del nodes[:] # Emptying array closes connections as a side effect

def set_node_times(nodes, t):
    for node in nodes:
        node.setmocktime(t)

def wait_bitcoinds():
    # Wait for all bitcoinds to cleanly exit
    for bitcoind in bitcoind_processes.values():
        bitcoind.wait()
    bitcoind_processes.clear()

def connect_nodes(from_connection, node_num):
    ip_port = "127.0.0.1:"+str(p2p_port(node_num))
    from_connection.addnode(ip_port, "onetry")
    # poll until version handshake complete to avoid race conditions
    # with transaction relaying
    while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
        time.sleep(0.1)
    #print "Connected node%d %s" % (node_num, ip_port)

def connect_nodes_bi(nodes, a, b):
    connect_nodes(nodes[a], b)
    connect_nodes(nodes[b], a)

def find_output(node, txid, amount):
    """
    Return index to output of txid with value amount
    Raises exception if there is none.
    """
    txdata = node.getrawtransaction(txid, 1)
    for i in range(len(txdata["vout"])):
        if txdata["vout"][i]["value"] == amount:
            return i
    raise RuntimeError("find_output txid %s : %s not found"%(txid,str(amount)))


def gather_inputs(from_node, amount_needed, confirmations_required=1):
    """
    Return a random set of unspent txouts that are enough to pay amount_needed
    """
    assert(confirmations_required >=0)
    utxo = from_node.listunspent(confirmations_required)
    random.shuffle(utxo)
    inputs = []
    total_in = Decimal("0.00000000")
    while total_in < amount_needed and len(utxo) > 0:
        t = utxo.pop()
        total_in += t["amount"]
        inputs.append({ "txid" : t["txid"], "vout" : t["vout"], "address" : t["address"] } )
    if total_in < amount_needed:
        raise RuntimeError("Insufficient funds: need %d, have %d"%(amount_needed, total_in))
    return (total_in, inputs)

def make_change(from_node, amount_in, amount_out, fee):
    """
    Create change output(s), return them
    """
    outputs = {}
    amount = amount_out+fee
    change = amount_in - amount
    if change > amount*2:
        # Create an extra change output to break up big inputs
        change_address = from_node.getnewaddress()
        # Split change in two, being careful of rounding:
        outputs[change_address] = Decimal(change/2).quantize(Decimal('0.00000001'), rounding=ROUND_DOWN)
        change = amount_in - amount - outputs[change_address]
    if change > 0:
        outputs[from_node.getnewaddress()] = change
    return outputs

def send_zeropri_transaction(from_node, to_node, amount, fee):
    """
    Create&broadcast a zero-priority transaction.
    Returns (txid, hex-encoded-txdata)
    Ensures transaction is zero-priority by first creating a send-to-self,
    then using its output
    """

    # Create a send-to-self with confirmed inputs:
    self_address = from_node.getnewaddress()
    (total_in, inputs) = gather_inputs(from_node, amount+fee*2)
    outputs = make_change(from_node, total_in, amount+fee, fee)
    outputs[self_address] = float(amount+fee)

    self_rawtx = from_node.createrawtransaction(inputs, outputs)
    self_signresult = from_node.signrawtransaction(self_rawtx)
    self_txid = from_node.sendrawtransaction(self_signresult["hex"], True)

    vout = find_output(from_node, self_txid, amount+fee)
    # Now immediately spend the output to create a 1-input, 1-output
    # zero-priority transaction:
    inputs = [ { "txid" : self_txid, "vout" : vout } ]
    outputs = { to_node.getnewaddress() : float(amount) }

    rawtx = from_node.createrawtransaction(inputs, outputs)
    signresult = from_node.signrawtransaction(rawtx)
    txid = from_node.sendrawtransaction(signresult["hex"], True)

    return (txid, signresult["hex"])

def random_zeropri_transaction(nodes, amount, min_fee, fee_increment, fee_variants):
    """
    Create a random zero-priority transaction.
    Returns (txid, hex-encoded-transaction-data, fee)
    """
    from_node = random.choice(nodes)
    to_node = random.choice(nodes)
    fee = min_fee + fee_increment*random.randint(0,fee_variants)
    (txid, txhex) = send_zeropri_transaction(from_node, to_node, amount, fee)
    return (txid, txhex, fee)

def random_transaction(nodes, amount, min_fee, fee_increment, fee_variants):
    """
    Create a random transaction.
    Returns (txid, hex-encoded-transaction-data, fee)
    """
    from_node = random.choice(nodes)
    to_node = random.choice(nodes)
    fee = min_fee + fee_increment*random.randint(0,fee_variants)

    (total_in, inputs) = gather_inputs(from_node, amount+fee)
    outputs = make_change(from_node, total_in, amount, fee)
    outputs[to_node.getnewaddress()] = float(amount)

    rawtx = from_node.createrawtransaction(inputs, outputs)
    signresult = from_node.signrawtransaction(rawtx)
    txid = from_node.sendrawtransaction(signresult["hex"], True)

    return (txid, signresult["hex"], fee)

def assert_equal(actual, expected, message = ""):
    if expected != actual:
        if message:
            message = "%s; " % message 
        raise AssertionError("%sexpected: <%s> but was: <%s>" % (message, str(expected), str(actual)))

def assert_true(condition, message = ""):
    if not condition:
        raise AssertionError(message)
        
def assert_false(condition, message = ""):
    assert_true(not condition, message)

def assert_greater_than(thing1, thing2):
    if thing1 <= thing2:
        raise AssertionError("%s <= %s"%(str(thing1),str(thing2)))

def assert_raises(exc, fun, *args, **kwds):
    try:
        fun(*args, **kwds)
    except exc:
        pass
    except Exception as e:
        raise AssertionError("Unexpected exception raised: "+type(e).__name__)
    else:
        raise AssertionError("No exception raised")

# Returns txid if operation was a success or None
def wait_and_assert_operationid_status(node, myopid, in_status='success', in_errormsg=None, timeout=300):
    print('waiting for async operation {}'.format(myopid))
    result = None
    for _ in range(1, timeout):
        results = node.z_getoperationresult([myopid])
        if len(results) > 0:
            result = results[0]
            break
        time.sleep(1)

    assert_true(result is not None, "timeout occured")
    status = result['status']

    txid = None
    errormsg = None
    if status == "failed":
        errormsg = result['error']['message']
    elif status == "success":
        txid = result['result']['txid']

    if os.getenv("PYTHON_DEBUG", ""):
        print('...returned status: {}'.format(status))
        if errormsg is not None:
            print('...returned error: {}'.format(errormsg))
    
    assert_equal(in_status, status, "Operation returned mismatched status. Error Message: {}".format(errormsg))

    if errormsg is not None:
        assert_true(in_errormsg is not None, "No error retured. Expected: {}".format(errormsg))
        assert_true(in_errormsg in errormsg, "Error returned: {}. Error expected: {}".format(errormsg, in_errormsg))
        return result # if there was an error return the result
    else:
        return txid # otherwise return the txid

def disconnect_nodes(from_connection, node_num):
    ip_port = "127.0.0.1:" + str(p2p_port(node_num))
    from_connection.disconnectnode(ip_port)
    # poll until version handshake complete to avoid race conditions
    # with transaction relaying
    while any(peer['version'] == 0 for peer in from_connection.getpeerinfo()):
        time.sleep(0.1)

def dump_ordered_tips(tip_list,debug=0):
    if debug == 0:
        return
    sorted_x = sorted(tip_list, key=lambda k: k['status'])
    c = 0
    for y in sorted_x:
        if (c == 0):
            print(y)
        else:
            print(" ", y)
        c = 1

def dump_sc_info_record(info, i, debug=0):
    if debug == 0:
        return
    print("  Node %d - balance: %f" % (i, info["balance"]))
    print("    created at block: %s (%d)" % (info["createdAtBlockHeight"], info["createdAtBlockHeight"]))
    print("    created in tx:    %s" % info["creatingTxHash"])
    print("    immatureAmounts: %s" % info["immatureAmounts"])

def dump_sc_info(nodes,nNodes,scId="",debug=0):
    if debug == 0:
        return
    if scId != "*":
        print("scid: " + scId)
        print("-------------------------------------------------------------------------------------")
        for i in range(0, nNodes):
            try:
                dump_sc_info_record(nodes[i].getscinfo(scId)['items'][0], i,debug)
            except JSONRPCException as e:
                print("  Node %d: ### [no such scid: %s]" % (i, scId))
    else:
        for i in range(0, nNodes):
            x = nodes[i].getscinfo("*")['items']
            for info in x:
                dump_sc_info_record(info, i,nNodes)

def colorize(color: str, text: str) -> str:
    """
    given an input string "text", returns the same string decorated with
    ANSI escape sequences for colored printing on *nix terminals.
    'color' is a single char used to select the color among:
        'e'     grey
        'r'     red
        'g'     green
        'y'     yellow
        'b'     blue
        'p'     purple
        'c'     cyan
        'n'     no color (white)
    """
    # As now, coloring is disabled on Windows systems
    if os.name == 'nt':
        return text

    if os.getenv('ANSI_COLORS_DISABLED') is not None:
        return text

    N_C = "\033[0m"
    colorshortcuts = ['e', 'r', 'g', 'y', 'b', 'p', 'c', 'n']
    color = color[:1]   # just get the first char of 'color' input param

    if color not in colorshortcuts:
        return text

    COLORS = dict(zip(colorshortcuts, list(range(90, 97)) + [0]))
    return "\033[%d;1m" % COLORS[color] + text + N_C

def mark_logs(msg, nodes, debug = 0, strip_escape = True):
    if debug == 0:
        return
    print(msg)
    # This regex removes all the escape sequences introduced by the colorize function
    if strip_escape:
        ansi_escape = re.compile(r'(\x9B|\x1B\[)[0-?]*[ -\/]*[@-~]')
        msg_clean = ansi_escape.sub('', msg)
    else:
        msg_clean = msg
    for node in nodes:
        node.dbg_log(msg_clean)

def get_end_epoch_height(scid, node, epochLen):
    sc_creating_height = node.getscinfo(scid)['items'][0]['createdAtBlockHeight']
    current_height = node.getblockcount()
    epoch_number = (current_height - sc_creating_height + 1) // epochLen - 1
    end_epoch_height = sc_creating_height - 1 + ((epoch_number + 1) * epochLen)
    return end_epoch_height

def get_epoch_data(scid, node, epochLen):
    sc_creating_height = node.getscinfo(scid)['items'][0]['createdAtBlockHeight']
    current_height = node.getblockcount()
    epoch_number = (current_height - sc_creating_height + 1) // epochLen - 1
    end_epoch_block_hash = node.getblockhash(sc_creating_height - 1 + ((epoch_number + 1) * epochLen))
    epoch_cum_tree_hash = node.getblock(end_epoch_block_hash)['scCumTreeHash']
    return epoch_number, epoch_cum_tree_hash

def get_spendable(node, min_amount):
    # get a UTXO in node's wallet with minimal amount
    utx = False
    listunspent = node.listunspent()
    for aUtx in listunspent:
        if aUtx['amount'] > min_amount:
            utx = aUtx
            change = aUtx['amount'] - min_amount
            break

    if utx == False:
        print(listunspent)

    assert_equal(utx!=False, True)
    return utx, change

def advance_epoch(mcTest, node, sync_call,
    scid, sc_tag, constant, epoch_length, cert_quality=1, cert_fee=Decimal("0.00001"),
    ftScFee=Decimal("0"), mbtrScFee=Decimal("0"), vCfe=[], vCmt=[], proofCfeArray=[], generateNumBlocks=-1):

    if (generateNumBlocks != 0):
        # if a nagative number is set, use epoch_length as a default, otherwise mine the number passed as a parameter
        if (generateNumBlocks > 0):
            node.generate(generateNumBlocks)
        else:
            node.generate(epoch_length)

    sync_call()

    epoch_number, epoch_cum_tree_hash = get_epoch_data(scid, node, epoch_length)

    scid_swapped = str(swap_bytes(scid))

    proof = mcTest.create_test_proof(
        sc_tag, scid_swapped, epoch_number, cert_quality, mbtrScFee, ftScFee, epoch_cum_tree_hash, constant, [], [], proofCfeArray)

    if proof == None:
        print("could not create proof")
        assert(False)

    try:
        cert = node.sc_send_certificate(scid, epoch_number, cert_quality,
            epoch_cum_tree_hash, proof, [], ftScFee, mbtrScFee, cert_fee, "", vCfe, vCmt)
    except JSONRPCException as e:
        errorString = e.error['message']
        print("Send certificate failed with reason {}".format(errorString))
        assert(False)
    sync_call()

    assert_true(cert in node.getrawmempool())

    return cert, epoch_number

def swap_bytes(input_buf):
    return codecs.encode(codecs.decode(input_buf, 'hex')[::-1], 'hex').decode()

def get_total_amount_from_listaddressgroupings(input_list):
    '''
    Assumes the list in input is obtained via the RPC cmd listaddressgroupings()
    '''
    tot_amount = Decimal("0.0")
    for group in input_list:
        for record in group:
            addr = record[0]
            val  = record[1]
            #print "Adding addr={}, val={}".format(addr, val)
            tot_amount += val
    return tot_amount

def to_satoshis(decimalAmount):
    return int(round(decimalAmount * COIN))

"""
    This function gets a Field Element hex string (typically shorter than 32 bytes)
    and adds padding zeros to reach the length of 32 bytes.

    Padding is prepended or appended depending on the sidechain version specified
    (see fork 9 for more details).
"""
def get_field_element_with_padding(field_element, sidechain_version):
    FIELD_ELEMENT_STRING_SIZE = 32 * 2

    if sidechain_version == 0:
        return field_element.rjust(FIELD_ELEMENT_STRING_SIZE, "0")
    elif sidechain_version == 1:
        return field_element.ljust(FIELD_ELEMENT_STRING_SIZE, "0")
    else:
        assert(False)
