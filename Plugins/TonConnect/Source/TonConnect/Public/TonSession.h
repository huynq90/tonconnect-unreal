#pragma once

#include "CoreMinimal.h"

// Wraps tweetnacl crypto operations needed for TON Connect:
//   - x25519 session keypair
//   - NaCl box encrypt/decrypt (bridge messages)
//   - Ed25519 sign/verify (ton_proof, P3)
//   - SHA-256 / SHA-512 hashing
//
// All byte arrays are TArray<uint8>. Nonces are 24 bytes (crypto_box_NONCEBYTES).
struct TONCONNECT_API FTonSession
{
    // 32-byte x25519 public key
    TArray<uint8> PublicKey;
    // 32-byte x25519 private key
    TArray<uint8> PrivateKey;

    // Generate a new session keypair. Call once per session.
    void GenerateKeyPair();

    bool IsValid() const { return PublicKey.Num() == 32 && PrivateKey.Num() == 32; }

    // NaCl box encrypt: nonce (24B) + ciphertext.
    // Returns false on failure.
    bool BoxEncrypt(
        const TArray<uint8>& PlainText,
        const TArray<uint8>& TheirPublicKey,
        TArray<uint8>& OutNonceAndCipher) const;

    // NaCl box decrypt: input is nonce (24B) || ciphertext.
    bool BoxDecrypt(
        const TArray<uint8>& NonceAndCipher,
        const TArray<uint8>& TheirPublicKey,
        TArray<uint8>& OutPlainText) const;

    // Generate 24 random bytes for a nonce.
    static TArray<uint8> GenerateNonce();

    // SHA-256 hash (32 bytes)
    static TArray<uint8> SHA256(const TArray<uint8>& Data);

    // SHA-512 hash (64 bytes)
    static TArray<uint8> SHA512(const TArray<uint8>& Data);

    // Ed25519 — used for ton_proof (P3). Signs arbitrary data.
    // OutSignature will be 64 bytes.
    bool Ed25519Sign(
        const TArray<uint8>& Message,
        TArray<uint8>& OutSignature) const;
};
