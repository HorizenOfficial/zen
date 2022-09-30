// Copyright (c) 2014 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "base58.h"

#include "version.h"
#include "streams.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <vector>
#include <string>
#include <boost/variant/apply_visitor.hpp>
#include <boost/variant/static_visitor.hpp>

CBase58Data::CBase58Data()
{
    vchVersion.clear();
    vchData.clear();
}

void CBase58Data::SetData(const std::vector<unsigned char>& vchVersionIn, const void* pdata, size_t nSize)
{
    vchVersion = vchVersionIn;
    vchData.resize(nSize);
    if (!vchData.empty())
        memcpy(&vchData[0], pdata, nSize);
}

void CBase58Data::SetData(const std::vector<unsigned char>& vchVersionIn, const unsigned char* pbegin, const unsigned char* pend)
{
    SetData(vchVersionIn, (void*)pbegin, pend - pbegin);
}

bool CBase58Data::SetString(const char* psz, unsigned int nVersionBytes)
{
    std::vector<unsigned char> vchTemp;
    bool rc58 = DecodeBase58Check(psz, vchTemp);
    if ((!rc58) || (vchTemp.size() < nVersionBytes)) {
        vchData.clear();
        vchVersion.clear();
        return false;
    }
    vchVersion.assign(vchTemp.begin(), vchTemp.begin() + nVersionBytes);
    vchData.resize(vchTemp.size() - nVersionBytes);
    if (!vchData.empty())
        memcpy(&vchData[0], &vchTemp[nVersionBytes], vchData.size());
    memory_cleanse(&vchTemp[0], vchData.size());
    return true;
}

bool CBase58Data::SetString(const std::string& str, unsigned int nVersionBytes)
{
    return SetString(str.c_str(), nVersionBytes);
}

std::string CBase58Data::ToString() const
{
    std::vector<unsigned char> vch = vchVersion;
    vch.insert(vch.end(), vchData.begin(), vchData.end());
    return EncodeBase58Check(vch);
}

int CBase58Data::CompareTo(const CBase58Data& b58) const
{
    if (vchVersion < b58.vchVersion)
        return -1;
    if (vchVersion > b58.vchVersion)
        return 1;
    if (vchData < b58.vchData)
        return -1;
    if (vchData > b58.vchData)
        return 1;
    return 0;
}

namespace
{
class CBitcoinAddressVisitor : public boost::static_visitor<bool>
{
private:
    CBitcoinAddress* addr;

public:
    CBitcoinAddressVisitor(CBitcoinAddress* addrIn) : addr(addrIn) {}

    bool operator()(const CKeyID& id) const { return addr->Set(id); }
    bool operator()(const CScriptID& id) const { return addr->Set(id); }
    bool operator()(const CNoDestination& no) const { return false; }
};

} // anon namespace

bool CBitcoinAddress::Set(const CKeyID& id)
{
	//TODO: Is the check regarding the id address created before or after chainsplit needed?
	// Now old addresses will receive new prefixes. Seems like that is not a problem but additional QA is needed.
    SetData(Params().Base58Prefix(CChainParams::PUBKEY_ADDRESS), &id, 20);
    return true;
}

bool CBitcoinAddress::Set(const CScriptID& id)
{
	//TODO: Is the check regarding the id address created before or after chainsplit needed?
	// Now old addresses will receive new prefixes. Seems like that is not a problem but additional QA is needed.
    SetData(Params().Base58Prefix(CChainParams::SCRIPT_ADDRESS), &id, 20);
    return true;
}

bool CBitcoinAddress::Set(const CTxDestination& dest)
{
    return boost::apply_visitor(CBitcoinAddressVisitor(this), dest);
}

bool CBitcoinAddress::IsValid() const
{
    return IsValid(Params());
}

bool CBitcoinAddress::IsValid(const CChainParams& params) const
{
    bool fCorrectSize = vchData.size() == 20;
    bool fKnownVersion = vchVersion == params.Base58Prefix(CChainParams::PUBKEY_ADDRESS) ||
                         vchVersion == params.Base58Prefix(CChainParams::SCRIPT_ADDRESS) ||
                         vchVersion == params.Base58Prefix(CChainParams::PUBKEY_ADDRESS_OLD) ||
                         vchVersion == params.Base58Prefix(CChainParams::SCRIPT_ADDRESS_OLD);
    return fCorrectSize && fKnownVersion;
}

bool CBitcoinAddress::SetString(const char* pszAddress)
{
    return CBase58Data::SetString(pszAddress, 2);
}

bool CBitcoinAddress::SetString(const std::string& strAddress)
{
    return SetString(strAddress.c_str());
}

CTxDestination CBitcoinAddress::Get() const
{
    if (!IsValid())
        return CNoDestination();
    uint160 id;
    memcpy(&id, &vchData[0], 20);
    if (vchVersion == Params().Base58Prefix(CChainParams::PUBKEY_ADDRESS)
    	|| vchVersion == Params().Base58Prefix(CChainParams::PUBKEY_ADDRESS_OLD))
        return CKeyID(id);
    else if (vchVersion == Params().Base58Prefix(CChainParams::SCRIPT_ADDRESS)
    		|| vchVersion == Params().Base58Prefix(CChainParams::SCRIPT_ADDRESS_OLD))
        return CScriptID(id);
    else
        return CNoDestination();
}

bool CBitcoinAddress::GetIndexKey(uint160& hashBytes, int& type) const
{
    if (!IsValid()) {
        return false;
    } else if (vchVersion == Params().Base58Prefix(CChainParams::PUBKEY_ADDRESS)
               || vchVersion == Params().Base58Prefix(CChainParams::PUBKEY_ADDRESS_OLD)) {
        memcpy(&hashBytes, &vchData[0], 20);
        type = 1;
        return true;
    } else if (vchVersion == Params().Base58Prefix(CChainParams::SCRIPT_ADDRESS)
               || vchVersion == Params().Base58Prefix(CChainParams::SCRIPT_ADDRESS_OLD)) {
        memcpy(&hashBytes, &vchData[0], 20);
        type = 2;
        return true;
    }

    return false;
}

bool CBitcoinAddress::GetKeyID(CKeyID& keyID) const
{
    if (!IsPubKey())
        return false;
    uint160 id;
    memcpy(&id, &vchData[0], 20);
    keyID = CKeyID(id);
    return true;
}

bool CBitcoinAddress::IsPubKey() const
{
    return IsValid() &&
    		(vchVersion == Params().Base58Prefix(CChainParams::PUBKEY_ADDRESS)
    		|| vchVersion == Params().Base58Prefix(CChainParams::PUBKEY_ADDRESS_OLD));
}

bool CBitcoinAddress::IsScript() const
{
    return IsValid() &&
    		(vchVersion == Params().Base58Prefix(CChainParams::SCRIPT_ADDRESS)
    		|| vchVersion == Params().Base58Prefix(CChainParams::SCRIPT_ADDRESS_OLD));
}

void CBitcoinSecret::SetKey(const CKey& vchSecret)
{
    assert(vchSecret.IsValid());
    SetData(Params().Base58Prefix(CChainParams::SECRET_KEY), vchSecret.begin(), vchSecret.size());
    if (vchSecret.IsCompressed())
        vchData.push_back(1);
}

CKey CBitcoinSecret::GetKey()
{
    CKey ret;
    assert(vchData.size() >= 32);
    ret.Set(vchData.begin(), vchData.begin() + 32, vchData.size() > 32 && vchData[32] == 1);
    return ret;
}

bool CBitcoinSecret::IsValid() const
{
    bool fExpectedFormat = vchData.size() == 32 || (vchData.size() == 33 && vchData[32] == 1);
    bool fCorrectVersion = vchVersion == Params().Base58Prefix(CChainParams::SECRET_KEY);
    return fExpectedFormat && fCorrectVersion;
}

bool CBitcoinSecret::SetString(const char* pszSecret)
{
    return CBase58Data::SetString(pszSecret, 1) && IsValid();
}

bool CBitcoinSecret::SetString(const std::string& strSecret)
{
    return SetString(strSecret.c_str());
}

template<class DATA_TYPE, CChainParams::Base58Type PREFIX, size_t SER_SIZE>
bool CZCEncoding<DATA_TYPE, PREFIX, SER_SIZE>::Set(const DATA_TYPE& addr)
{
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << addr;
    std::vector<unsigned char> addrSerialized(ss.begin(), ss.end());
    assert(addrSerialized.size() == SER_SIZE);
    SetData(Params().Base58Prefix(PREFIX), &addrSerialized[0], SER_SIZE);
    return true;
}

template<class DATA_TYPE, CChainParams::Base58Type PREFIX, size_t SER_SIZE>
DATA_TYPE CZCEncoding<DATA_TYPE, PREFIX, SER_SIZE>::Get() const
{
    if (vchData.size() != SER_SIZE) {
        throw std::runtime_error(
            PrependName(" is invalid")
        );
    }

    if (vchVersion != Params().Base58Prefix(PREFIX)) {
        throw std::runtime_error(
            PrependName(" is for wrong network type")
        );
    }

    std::vector<unsigned char> serialized(vchData.begin(), vchData.end());

    CDataStream ss(serialized, SER_NETWORK, PROTOCOL_VERSION);
    DATA_TYPE ret;
    ss >> ret;
    return ret;
}

// Explicit instantiations for libzcash::PaymentAddress
template bool CZCEncoding<libzcash::PaymentAddress,
                          CChainParams::ZCPAYMENT_ADDRRESS,
                          libzcash::SerializedPaymentAddressSize>::Set(const libzcash::PaymentAddress& addr);
template libzcash::PaymentAddress CZCEncoding<libzcash::PaymentAddress,
                                              CChainParams::ZCPAYMENT_ADDRRESS,
                                              libzcash::SerializedPaymentAddressSize>::Get() const;

// Explicit instantiations for libzcash::ViewingKey
template bool CZCEncoding<libzcash::ViewingKey,
                          CChainParams::ZCVIEWING_KEY,
                          libzcash::SerializedViewingKeySize>::Set(const libzcash::ViewingKey& vk);
template libzcash::ViewingKey CZCEncoding<libzcash::ViewingKey,
                                          CChainParams::ZCVIEWING_KEY,
                                          libzcash::SerializedViewingKeySize>::Get() const;

// Explicit instantiations for libzcash::SpendingKey
template bool CZCEncoding<libzcash::SpendingKey,
                          CChainParams::ZCSPENDING_KEY,
                          libzcash::SerializedSpendingKeySize>::Set(const libzcash::SpendingKey& sk);
template libzcash::SpendingKey CZCEncoding<libzcash::SpendingKey,
                                           CChainParams::ZCSPENDING_KEY,
                                           libzcash::SerializedSpendingKeySize>::Get() const;
