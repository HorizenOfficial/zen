#include "uint256.h"

struct CMaturityHeightIteratorKey {
    int blockHeight;

    size_t GetSerializeSize(int nType, int nVersion) const { return 4; }
    template <typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        // Heights are stored big-endian for key sorting in LevelDB
        ser_writedata32be(s, blockHeight);
    }
    template <typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        blockHeight = ser_readdata32be(s);
    }

    CMaturityHeightIteratorKey(int height) { blockHeight = height; }

    CMaturityHeightIteratorKey() { SetNull(); }

    void SetNull() { blockHeight = 0; }
};

struct CMaturityHeightKey {
    int blockHeight;
    uint256 certId;

    size_t GetSerializeSize(int nType, int nVersion) const { return 36; }
    template <typename Stream>
    void Serialize(Stream& s, int nType, int nVersion) const {
        // Heights are stored big-endian for key sorting in LevelDB
        ser_writedata32be(s, blockHeight);
        certId.Serialize(s, nType, nVersion);
    }
    template <typename Stream>
    void Unserialize(Stream& s, int nType, int nVersion) {
        blockHeight = ser_readdata32be(s);
        certId.Unserialize(s, nType, nVersion);
    }

    CMaturityHeightKey(int height, uint256 hash) {
        blockHeight = height;
        certId = hash;
    }

    CMaturityHeightKey() { SetNull(); }

    void SetNull() {
        blockHeight = 0;
        certId.SetNull();
    }
};

// This is needed because the CLevelDBBatch.Write requires 2 arguments (key, value)
struct CMaturityHeightValue {
    char dummy;

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
        READWRITE(dummy);
    }

    CMaturityHeightValue(char value) { dummy = value; }

    CMaturityHeightValue() { SetNull(); }

    void SetNull() { dummy = static_cast<char>(0); }

    bool IsNull() const { return dummy == static_cast<char>(0); }
};