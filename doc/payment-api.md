# Horizen Payment API

## Overview

### Standard Horizen payments

Horizen inherits the Bitcoin Core API to support standard blockchain payments through transparent addresses, often called `taddr`, which store value in UTXOs.

When transferring funds from one `taddr` to another `taddr`, you can use either the existing Bitcoin RPC calls or other ones inherited from Zcash and that were originally meant to support shielded transactions (now completely removed from Horizen).

### Horizen Sidechains

Horizen further extends the Bitcoin Core API with new RPC calls to support the creation of sidechains and bi-directional coin transfers between mainchain and sidechains.

These RPC calls comprise the user interface to the Cross Chain Transfer Protocol (CCTP) implementation.

CCTP makes use of two address formats:

* mainchain: only transparent funds can be used, and only P2PKH transparent addresses can be used
* sidechain: addresses, also called PublicKey25519Propositions, are represented by a 32 long hex string

When funds are transferred from mainchain to sidechain, no corresponding UTXOs are created in the mainchain because funds are made available on the sidechain side, this means that amounts sent from the mainchain to a sidechain are burnt on the mainchain.

When funds are transferred from sidechain back to mainchain new UTXOs are generated on mainchain.

## Compatibility with Bitcoin Core

Horizen supports all commands in the Bitcoin Core API (as of version 0.11.2). Where applicable, Horizen will extend commands in a backwards-compatible way to enable additional functionality.

We do not recommend use of accounts which are now deprecated in Bitcoin Core.  Where the account parameter exists in the API, please use “” as its value, otherwise an error will be returned.

To support multiple users in a single node’s wallet, consider using `getnewaddress` to obtain a new address for each user.  Also consider mapping multiple addresses to each user.

## List of Horizen API commands

Optional parameters are denoted in [square brackets].

RPC calls by category:

* Payment : `z_sendmany`, `z_mergetoaddress`
* Sidechains: `sc_create`, `sc_send`, `sc_request_transfer`, `sc_send_certificate`

RPC parameter conventions:

* `taddr` or `address`: Transparent address
* `amount` : JSON format double-precision number with 1 ZEN expressed as 1.00000000

### Payment

Command | Parameters | Description
--- | --- | ---
z_sendmany<br> | fromaddress amounts [minconf=1] [fee=0.0001] | Send funds from an address to multiple outputs.  The address must be a `taddr`.<br><br>Amounts is a list containing key/value pairs corresponding to the addresses and amount to pay.  Each output address must be in `taddr` format.<br><br>Example of Outputs parameter:<br>`[{“address”:”zn123…”, “amount”:0.005},<br>,{“address”:”zc010…”,”amount”:0.03}]`<br><br>Optionally set the minimum number of confirmations which a transaction must have in order to be used as an input.<br><br>Optionally set a transaction fee, which by default is 0.0001 ZEN.<br><br>Any change will be sent to a new address.<br>

### Sidechains
Command | Parameters | Description
--- | --- | ---
sc_create <br>| params | Create a Sidechain and send funds to it.<br><br>**params** is a list containing key/value pairs:<br><br>"withdrawalEpochLength" : epoch &emsp;- _length of the withdrawal epochs_<br>"toaddress" : scaddr &emsp;- _The receiver PublicKey25519Proposition in the Sidechain_<br>"amount" : amount &emsp;- _Funds to be sent to the newly created Sidechain_<br>"wCertVk" : data &emsp;- _Verification key expressed in hexadecimal format, required to verify a Certificate SC proof._<br><br>optional key/value pairs:<br><br>["fromaddress" : taddr]&emsp;- _The taddr to send the funds from. If omitted funds are taken from all available UTXO_<br>["changeaddress" : taddr] &emsp;- _The taddr to send the change to, if any. If not set, "fromaddress" is used. If the latter is not set too, a newly generated address will be used_<br>["minconf" : conf] &emsp;- _Only use funds confirmed at least this many times. Default=1._<br>["fee" : fee] &emsp;- _The fee amount to attach to this transaction._<br>["customData" : data] &emsp;- _Generic custom data in hexadecimal format._<br>["constant" : data] &emsp; - _Data in hexadecimal format used as public input for WCert proof verification. Its size must be 32 bytes_<br>["wCeasedVk" : data]  &emsp; - _Verification key expressed in hexadecimal format, required to verify a Ceased sidechain withdrawal SC proof if supported._<br>["vFieldElementCertificateFieldConfig"] &emsp; - _An array whose entries are sizes (in bits). Any certificate should have as many custom FieldElements with the corresponding size._<br>["vBitVectorCertificateFieldConfig"] &emsp; - _An array whose entries are bitVectorSizeBits and maxCompressedSizeBytes pairs. Any certificate should have as many custom BitVectorCertificateField with the corresponding sizes_<br>["forwardTransferScFee" : fee] &emsp; - _The amount of fee due to sidechain actors when creating a forward transfer. Default=0_<br>["mainchainBackwardTransferScFee" : fee] &emsp; - _The amount of fee due to sidechain actors when creating a MBTR. Default=0_<br>["mainchainBackwardTransferRequestDataLength" : len] &emsp; - _The expected size of the request data vector in a MBTR. Default=0_<br><br>Returns a list with two key/value pairs:<br><br>"txid":transaction ID<br>"scid":sidechain ID<br>
sc_send <br>|outputs [params]|Send funds to a sidechain<br><br>**outputs** is an array of objects of key/value pairs representing the amounts to send:<br><br>"scid" : id &emsp;- _Sidechain ID_<br>"toaddress" : scaddr &emsp; - _The receiver PublicKey25519Proposition in the SC_<br>"amount" : amount &emsp;- _The amount to transfer_<br>"mcReturnAddress" : taddr &emsp;- _The Horizen address where to send the backward transfer in case Forward Transfer is rejected by the sidechain_<br><br>**params** is an object of optional key/value pairs :<br><br>["fromaddress" : taddr]&emsp;- _The taddr to send the funds from. If omitted funds are taken from all available UTXO_<br>["changeaddress" : taddr] &emsp;- _The taddr to send the change to, if any. If not set, "fromaddress" is used. If the latter is not set too, a newly generated address will be used_<br>["minconf" : conf] &emsp;- _Only use funds confirmed at least this many times. Default=1._<br>["fee" : fee] &emsp;- _The fee amount to attach to this transaction._<br><br>Return:<br><br>"txid": the transaction ID<br>
sc_request_transfer <br>|outputs [params]|Request a list of sidechains to send some backward transfer to mainchain in one of the next certificates<br><br>**outputs** is an array of objects of key/value pairs representing the requests to send:<br><br>"scid" : id &emsp;- _Sidechain ID_<br>"vScRequestData" : array &emsp; - _It is an arbitrary array of data in hexadecimal format representing a SC reference (for instance an Utxo ID) for which a backward transfer is being requested._<br>"scFee" : fee &emsp;- _the value spent by the sender that will be gained by a SC block forger_<br>"mcDestinationAddress" : taddr &emsp;- _The Horizen mainchain address where to send the requested backward transfer_<br><br>**params** is an object of optional key/value pairs :<br><br>["fromaddress" : taddr]&emsp;- _The taddr to send the funds from. If omitted funds are taken from all available UTXO_<br>["changeaddress" : taddr] &emsp;- _The taddr to send the change to, if any. If not set, "fromaddress" is used. If the latter is not set too, a newly generated address will be used_<br>["minconf" : conf] &emsp;- _Only use funds confirmed at least this many times. Default=1._<br>["fee" : fee] &emsp;- _The mainchain fee amount to attach to this transaction._<br><br>Return:<br><br>"txid": the transaction ID<br>
sc_send_certificate <br>|scid<br>epochNumber<br>quality<br> endEpochCumScTxCommTreeRoot<br>scProof transfers<br> forwardTransferScFee<br>mainchainBackwardTransferScFee<br>[fee]<br>[fromAddress]<br>[vFieldElementCertificateField]<br>[vBitVectorCertificateField]<br>|Send cross chain backward transfers from SC to MC as a certificate.<br><br>**scid**: the sidechain ID<br>**epochNumber**: The epoch number this certificate refers to, zero-based numbering.<br>**quality**: The quality of this withdrawal certificate.<br>**endEpochCumScTxCommTreeRoot**: The hex string representation of the field element corresponding to the root of the cumulative scTxCommitment tree stored at the block marking the end of the referenced epoch.<br>**scProof**: SNARK proof whose verification key wCertVk was set at sidechain creation.<br>**transfers**: a list of key/value pairs representing the amounts of the backward transfers. Can also be empty:<br>&emsp;&emsp;"address" : taddr &emsp; - _The Horizen mainchain address of the receiver_<br>&emsp;&emsp;"amount" : amount &emsp; - _The amount of the backward transfer_<br>**forwardTransferScFee**: The amount of fee due to sidechain actors when creating a FT.<br>**mainchainBackwardTransferScFee**: The amount of fee due to sidechain actors when creating a MBTR.<br>**fee**: The mainchain fee amount to attach to this certificate.<br>**fromAddress**: The taddr to send the coins from. If omitted, coins are chosen among all available UTXO.<br>**vFieldElementCertificateField**: a list of hexadecimal strings each of them representing data used to verify the SNARK proof of the certificate <br>**vBitVectorCertificateField**: a list of hexadecimal strings each of them representing a compressed bit vector used to verify the SNARK proof of the certificate<br><br>Return:<br><br>"txid": the certificate ID<br>


## RPC call Error Codes

Horizen error codes are defined in https://github.com/HorizenOfficial/zen/blob/master/src/rpcprotocol.h

### z_sendmany error codes

RPC_INVALID_PARAMETER (-8) | _Invalid, missing or duplicate parameter_
---------------------------| -------------------------------------------------
"Minconf cannot be negative" | Cannot accept negative minimum confirmation number.
"Minimum number of confirmations cannot be less than 0" | Cannot accept negative minimum confirmation number.
"From address parameter missing" | Missing an address to send funds from.
"No recipients" | Missing recipient addresses.
"Invalid parameter, expected object" | Expected object.
"Invalid parameter, unknown key: __" | Unknown key.
"Invalid parameter, expected valid size" | Invalid size.
"Invalid parameter, expected hex txid" | Invalid txid.
"Invalid parameter, vout must be positive" | Invalid vout.
"Invalid parameter, duplicated address" | Address is duplicated.
"Invalid parameter, amounts array is empty" | Amounts array is empty.
"Invalid parameter, unknown key" | Key not found.
"Invalid parameter, unknown address format" | Unknown address format.
"Invalid parameter, amount must be positive" | Invalid or negative amount.


RPC_INVALID_ADDRESS_OR_KEY (-5) | _Invalid address or key_
--------------------------------| ---------------------------
"Invalid output address, not a valid taddr."            | Transparent output address is invalid.
"Invalid from address, must be a taddr."     | Sender address is invalid.


RPC_WALLET_INSUFFICIENT_FUNDS (-6) | _Not enough funds in wallet or account_
-----------------------------------| ------------------------------------------
"Insufficient funds, no UTXOs found for taddr from address." | Insufficient funds for sending address.
"Could not find any non-coinbase UTXOs to spend." | No available non-coinbase UTXOs.
"Insufficient funds for sending address.
"Insufficient transparent funds, have __, need __ plus fee __" | Insufficient funds from transparent address.


RPC_WALLET_ERROR (-4) | _Unspecified problem with wallet_
----------------------| -------------------------------------
"Not enough funds to pay miners fee" | Retry with sufficient funds.
"Missing hex data for raw transaction" | Raw transaction data is null.
"Missing hex data for signed transaction" | Hex value for signed transaction is null.
"Send raw transaction did not return an error or a txid." |


RPC_WALLET_ENCRYPTION_FAILED (-16)                                       | _Failed to encrypt the wallet_
-------------------------------------------------------------------------| -------------------------------------
"Failed to sign transaction"                                             | Transaction was not signed, sign transaction and retry.


RPC_WALLET_KEYPOOL_RAN_OUT (-12)                                         | _Keypool ran out, call keypoolrefill first_
-------------------------------------------------------------------------| -----------------------------------------------
"Could not generate a `taddr` to use as a change address"                  | Call keypoolrefill and retry.
