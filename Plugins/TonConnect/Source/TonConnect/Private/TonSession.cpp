#include "TonSession.h"

THIRD_PARTY_INCLUDES_START
#pragma push_macro("check")
#undef check
extern "C" {
#include "tweetnacl.h"
}
#pragma pop_macro("check")
THIRD_PARTY_INCLUDES_END

// Defined in TonCryptoAdapter.cpp which compiles tweetnacl.c
extern "C" void randombytes(unsigned char* buf, unsigned long long len);

void FTonSession::GenerateKeyPair()
{
    PublicKey.SetNum(crypto_box_PUBLICKEYBYTES);
    PrivateKey.SetNum(crypto_box_SECRETKEYBYTES);
    crypto_box_keypair(PublicKey.GetData(), PrivateKey.GetData());
}

bool FTonSession::BoxEncrypt(const TArray<uint8>& PlainText, const TArray<uint8>& TheirPublicKey,
                              TArray<uint8>& OutNonceAndCipher) const
{
    if (!IsValid() || TheirPublicKey.Num() != 32) return false;

    TArray<uint8> Nonce = GenerateNonce();

    // NaCl box requires 32 zero-byte prefix on the plaintext
    TArray<uint8> PaddedMsg;
    PaddedMsg.SetNumZeroed(crypto_box_ZEROBYTES);
    PaddedMsg.Append(PlainText);

    TArray<uint8> CipherBuf;
    CipherBuf.SetNum(PaddedMsg.Num());

    if (crypto_box(CipherBuf.GetData(), PaddedMsg.GetData(), (unsigned long long)PaddedMsg.Num(),
                   Nonce.GetData(), TheirPublicKey.GetData(), PrivateKey.GetData()) != 0)
    {
        return false;
    }

    // Wire format: nonce (24B) || ciphertext without the 16 leading zero bytes
    OutNonceAndCipher.Reset();
    OutNonceAndCipher.Append(Nonce);
    OutNonceAndCipher.Append(CipherBuf.GetData() + crypto_box_BOXZEROBYTES,
                              CipherBuf.Num() - crypto_box_BOXZEROBYTES);
    return true;
}

bool FTonSession::BoxDecrypt(const TArray<uint8>& NonceAndCipher, const TArray<uint8>& TheirPublicKey,
                              TArray<uint8>& OutPlainText) const
{
    if (!IsValid() || TheirPublicKey.Num() != 32) return false;

    const int32 NonceLen = crypto_box_NONCEBYTES;
    if (NonceAndCipher.Num() <= NonceLen) return false;

    TArray<uint8> Nonce(NonceAndCipher.GetData(), NonceLen);

    // Restore NaCl wire format: prepend 16 zero bytes before ciphertext
    TArray<uint8> PaddedCipher;
    PaddedCipher.SetNumZeroed(crypto_box_BOXZEROBYTES);
    PaddedCipher.Append(NonceAndCipher.GetData() + NonceLen, NonceAndCipher.Num() - NonceLen);

    TArray<uint8> PlainBuf;
    PlainBuf.SetNum(PaddedCipher.Num());

    if (crypto_box_open(PlainBuf.GetData(), PaddedCipher.GetData(), (unsigned long long)PaddedCipher.Num(),
                        Nonce.GetData(), TheirPublicKey.GetData(), PrivateKey.GetData()) != 0)
    {
        return false;
    }

    // Strip the 32-byte zero prefix from decrypted message
    OutPlainText.Reset();
    OutPlainText.Append(PlainBuf.GetData() + crypto_box_ZEROBYTES, PlainBuf.Num() - crypto_box_ZEROBYTES);
    return true;
}

TArray<uint8> FTonSession::GenerateNonce()
{
    TArray<uint8> Nonce;
    Nonce.SetNum(crypto_box_NONCEBYTES);
    randombytes(Nonce.GetData(), (unsigned long long)Nonce.Num());
    return Nonce;
}

TArray<uint8> FTonSession::SHA256(const TArray<uint8>& Data)
{
    TArray<uint8> Out;
    Out.SetNum(crypto_hash_sha256_BYTES);
    crypto_hash_sha256(Out.GetData(), Data.GetData(), (unsigned long long)Data.Num());
    return Out;
}

TArray<uint8> FTonSession::SHA512(const TArray<uint8>& Data)
{
    TArray<uint8> Out;
    Out.SetNum(crypto_hash_sha512_BYTES);
    crypto_hash_sha512(Out.GetData(), Data.GetData(), (unsigned long long)Data.Num());
    return Out;
}

bool FTonSession::Ed25519Sign(const TArray<uint8>& Message, TArray<uint8>& OutSignature) const
{
    // P3: ton_proof requires a separate Ed25519 signing keypair.
    // The session key is x25519 (crypto_box), not Ed25519 (crypto_sign).
    return false;
}
