#include "libsodium_ut_sign.h"

#include "unity.h"
//#include "aether/tele.h"

namespace sign
{
    #define DEBUG_OUT 1
    #define LOG(TAG, ...) {\
     fprintf(stderr, "file location%s\nfunction name:%s\nline:%d\n", __FILE__, __FUNCTION__, __LINE__);\
     fprintf(stderr, "date:%s\ntime:%s\n",__DATE__, __TIME__);\
     fprintf(stderr, "%s", TAG);\
     fprintf(stderr, "\r\n");\
     fprintf(stderr, __VA_ARGS__);\
     fprintf(stderr, "\n");\
    }

    static const char *TAG = "SODIUM UNITY";

    #if DEBUG_OUT==1
        #define DebugPrint(format, ...) LOG(TAG, format, ##__VA_ARGS__)
    #elif DEBUG_OUT==2
        #define DebugPrint(format, ...) AE_TELED_DEBUG(TAG, format, ##__VA_ARGS__)
    #elif DEBUG_OUT==3
        #define DebugPrint(format, ...) ESP_LOGI(TAG, format, ##__VA_ARGS__)
    #endif

    int
    test(void)
    {
        crypto_sign_state  st;
        unsigned char      extracted_seed[crypto_sign_ed25519_SEEDBYTES];
        unsigned char      extracted_pk[crypto_sign_ed25519_PUBLICKEYBYTES];
        unsigned char      sig[crypto_sign_BYTES];
        unsigned char      sm[1024 + crypto_sign_BYTES];
        unsigned char      m[1024];
        unsigned char      skpk[crypto_sign_SECRETKEYBYTES];
        unsigned char      pk[crypto_sign_PUBLICKEYBYTES];
        unsigned char      sk[crypto_sign_SECRETKEYBYTES];
        char               sig_hex[crypto_sign_BYTES * 2 + 1];
        char               pk_hex[crypto_sign_PUBLICKEYBYTES * 2 + 1];
        char               sk_hex[crypto_sign_SECRETKEYBYTES * 2 + 1];
        unsigned long long siglen;
        unsigned long long smlen;
        unsigned long long mlen;
        unsigned int       i;
        unsigned int       j;

        memset(sig, 0, sizeof sig);
        for (i = 0U; i < (sizeof test_data) / (sizeof test_data[0]); i++) {
    #ifdef BROWSER_TESTS
            if (i % 128U != 127U) {
                continue;
            }
    #endif
            memcpy(skpk, test_data[i].sk, crypto_sign_SEEDBYTES);
            memcpy(skpk + crypto_sign_SEEDBYTES, test_data[i].pk,
                   crypto_sign_PUBLICKEYBYTES);
            if (crypto_sign(sm, &smlen, (const unsigned char *)test_data[i].m, i,
                            skpk) != 0) {
                DebugPrint("crypto_sign() failure: [%u]\n", i);
                continue;
            }
            if (memcmp(test_data[i].sig, sm, crypto_sign_BYTES) != 0) {
                DebugPrint("signature failure: [%u]\n", i);
                continue;
            }
            if (crypto_sign_open(m, NULL, sm, smlen, test_data[i].pk) != 0) {
                DebugPrint("crypto_sign_open() failure: [%u]\n", i);
                continue;
            }
            add_l(sm + 32);
    #ifndef ED25519_COMPAT
            if (crypto_sign_open(m, &mlen, sm, smlen, test_data[i].pk) != -1) {
                DebugPrint("crypto_sign_open(): signature [%u] is malleable\n", i);
                continue;
            }
    #else
            if (crypto_sign_open(m, &mlen, sm, smlen, test_data[i].pk) != 0) {
                DebugPrint("crypto_sign_open(): signature [%u] is not malleable\n", i);
                continue;
            }
    #endif
            if (memcmp(test_data[i].m, m, (size_t)mlen) != 0) {
                DebugPrint("message verification failure: [%u]\n", i);
                continue;
            }
            sm[i + crypto_sign_BYTES - 1U]++;
            if (crypto_sign_open(m, &mlen, sm, smlen, test_data[i].pk) == 0) {
                DebugPrint("message can be forged: [%u]\n", i);
                continue;
            }
            if (crypto_sign_open(m, &mlen, sm, i % crypto_sign_BYTES,
                                 test_data[i].pk) == 0) {
                DebugPrint("short signed message verifies: [%u]\n",
                       i % crypto_sign_BYTES);
                continue;
            }
            if (crypto_sign_detached(sig, &siglen,
                                     (const unsigned char *)test_data[i].m, i, skpk)
                != 0) {
                DebugPrint("detached signature failed: [%u]\n", i);
                continue;
            }
            if (siglen == 0U || siglen > crypto_sign_BYTES) {
                DebugPrint("detached signature has an unexpected length: [%u]\n", i);
                continue;
            }
            if (memcmp(test_data[i].sig, sig, crypto_sign_BYTES) != 0) {
                DebugPrint("detached signature failure: [%u]\n", i);
                continue;
            }
            if (crypto_sign_verify_detached(sig,
                                            (const unsigned char *)test_data[i].m,
                                            i, test_data[i].pk) != 0) {
                DebugPrint("detached signature verification failed: [%u]\n", i);
                continue;
            }
        }
        DebugPrint("%u tests\n", i);

        i--;

        memcpy(sm, test_data[i].m, i);
        if (crypto_sign(sm, &smlen, sm, i, skpk) != 0) {
            DebugPrint("crypto_sign() with overlap failed\n");
        }
        if (crypto_sign_open(sm, &mlen, sm, smlen, test_data[i].pk) != 0) {
            DebugPrint("crypto_sign_open() with overlap failed\n");
        }
        if (memcmp(test_data[i].m, sm, (size_t)mlen) != 0) {
            DebugPrint("crypto_sign_open() with overlap failed (content)\n");
        }

        for (j = 1U; j < 8U; j++) {
            sig[63] ^= (j << 5);
            if (crypto_sign_verify_detached(sig,
                                            (const unsigned char *)test_data[i].m,
                                            i, test_data[i].pk) != -1) {
                DebugPrint("detached signature verification should have failed\n");
                continue;
            }
            sig[63] ^= (j << 5);
        }

    #ifndef ED25519_COMPAT
        if (crypto_sign_verify_detached(sig,
                                        (const unsigned char *)test_data[i].m,
                                        i, non_canonical_p) != -1) {
            DebugPrint("detached signature verification with non-canonical key should have failed\n");
        }
    #endif
        memset(pk, 0, sizeof pk);
        if (crypto_sign_verify_detached(sig,
                                        (const unsigned char *)test_data[i].m,
                                        i, pk) != -1) {
            DebugPrint("detached signature verification should have failed\n");
        }

        memset(sig, 0xff, 32);
        sig[0] = 0xdb;
        if (crypto_sign_verify_detached(sig,
                                        (const unsigned char *)test_data[i].m,
                                        i, pk) != -1) {
            DebugPrint("detached signature verification should have failed\n");
        }
        TEST_ASSERT(crypto_sign_detached(sig, NULL,
                                    (const unsigned char *)test_data[i].m, i, skpk) == 0);

        sodium_hex2bin(pk, crypto_sign_PUBLICKEYBYTES,
                       "3eee494fb9eac773144e34b0c755affaf33ea782c0722e5ea8b150e61209ab36",
                       crypto_sign_PUBLICKEYBYTES * 2, NULL, NULL, NULL);
        if (crypto_sign_verify_detached(sig,
                                        (const unsigned char *)test_data[i].m,
                                        i, pk) != -1) {
            DebugPrint("signature with an invalid public key should have failed\n");
        }

        sodium_hex2bin(pk, crypto_sign_PUBLICKEYBYTES,
                       "0200000000000000000000000000000000000000000000000000000000000000",
                       crypto_sign_PUBLICKEYBYTES * 2, NULL, NULL, NULL);
        if (crypto_sign_verify_detached(sig,
                                        (const unsigned char *)test_data[i].m,
                                        i, pk) != -1) {
            DebugPrint("signature with an invalid public key should have failed\n");
        }

        sodium_hex2bin(pk, crypto_sign_PUBLICKEYBYTES,
                       "0500000000000000000000000000000000000000000000000000000000000000",
                       crypto_sign_PUBLICKEYBYTES * 2, NULL, NULL, NULL);
        if (crypto_sign_verify_detached(sig,
                                        (const unsigned char *)test_data[i].m,
                                        i, pk) != -1) {
            DebugPrint("signature with an invalid public key should have failed\n");
        }

        if (crypto_sign_seed_keypair(pk, sk, keypair_seed) != 0) {
            DebugPrint("crypto_sign_seed_keypair() failure\n");
            return -1;
        }
        crypto_sign_init(&st);
        crypto_sign_update(&st, (const unsigned char *)test_data[i].m, i);
        crypto_sign_final_create(&st, sig, NULL, sk);
        sodium_bin2hex(sig_hex, sizeof sig_hex, sig, sizeof sig);
        DebugPrint("ed25519ph sig: [%s]\n", sig_hex);

        crypto_sign_init(&st);
        crypto_sign_update(&st, (const unsigned char *)test_data[i].m, i);
        if (crypto_sign_final_verify(&st, sig, pk) != 0) {
            DebugPrint("ed5519ph verification failed\n");
        }
        crypto_sign_init(&st);
        crypto_sign_update(&st, (const unsigned char *)test_data[i].m, 0);
        crypto_sign_update(&st, (const unsigned char *)test_data[i].m, i / 2);
        crypto_sign_update(&st, ((const unsigned char *)test_data[i].m) + i / 2,
                           i - i / 2);
        if (crypto_sign_final_verify(&st, sig, pk) != 0) {
            DebugPrint("ed5519ph verification failed\n");
        }
        sig[0]++;
        if (crypto_sign_final_verify(&st, sig, pk) != -1) {
            DebugPrint("ed5519ph verification could be forged\n");
        }
        sig[0]--;
        pk[0]++;
        if (crypto_sign_final_verify(&st, sig, pk) != -1) {
            DebugPrint("ed5519ph verification could be forged\n");
        }
        sodium_hex2bin(sk, crypto_sign_SECRETKEYBYTES,
                       "833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42",
                       2 * crypto_sign_SECRETKEYBYTES / 2, NULL, NULL, NULL);
        sodium_hex2bin(pk, crypto_sign_PUBLICKEYBYTES,
                       "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
                       2 * crypto_sign_PUBLICKEYBYTES, NULL, NULL, NULL);
        memcpy(sk + crypto_sign_SECRETKEYBYTES - crypto_sign_PUBLICKEYBYTES,
               pk, crypto_sign_PUBLICKEYBYTES);
        crypto_sign_init(&st);
        crypto_sign_update(&st, (const unsigned char *) "abc", 3);
        crypto_sign_final_create(&st, sig, &siglen, sk);
        if (siglen == 0U || siglen > crypto_sign_BYTES) {
            DebugPrint("ed25519ph signature has an unexpected length\n");
        }
        sodium_bin2hex(sig_hex, sizeof sig_hex, sig, sizeof sig);
        DebugPrint("ed25519ph tv sig: [%s]\n", sig_hex);

        crypto_sign_init(&st);
        crypto_sign_update(&st, (const unsigned char *) "abc", 3);
        if (crypto_sign_final_verify(&st, sig, pk) != 0) {
            DebugPrint("ed25519ph verification failed\n");
        }
        if (crypto_sign_keypair(pk, sk) != 0) {
            DebugPrint("crypto_sign_keypair() failure\n");
        }
        if (crypto_sign_seed_keypair(pk, sk, keypair_seed) != 0) {
            DebugPrint("crypto_sign_seed_keypair() failure\n");
            return -1;
        }
        crypto_sign_ed25519_sk_to_seed(extracted_seed, sk);
        if (memcmp(extracted_seed, keypair_seed, crypto_sign_ed25519_SEEDBYTES)
            != 0) {
            DebugPrint("crypto_sign_ed25519_sk_to_seed() failure\n");
        }
        crypto_sign_ed25519_sk_to_pk(extracted_pk, sk);
        if (memcmp(extracted_pk, pk, crypto_sign_ed25519_PUBLICKEYBYTES) != 0) {
            DebugPrint("crypto_sign_ed25519_sk_to_pk() failure\n");
        }
        sodium_bin2hex(pk_hex, sizeof pk_hex, pk, sizeof pk);
        sodium_bin2hex(sk_hex, sizeof sk_hex, sk, sizeof sk);

        DebugPrint("pk: [%s]\n", pk_hex);
        DebugPrint("sk: [%s]\n", sk_hex);

        TEST_ASSERT(crypto_sign_bytes() > 0U);
        TEST_ASSERT(crypto_sign_seedbytes() > 0U);
        TEST_ASSERT(crypto_sign_publickeybytes() > 0U);
        TEST_ASSERT(crypto_sign_secretkeybytes() > 0U);
        TEST_ASSERT(crypto_sign_messagebytes_max() > 0U);
        TEST_ASSERT(strcmp(crypto_sign_primitive(), "ed25519") == 0);
        TEST_ASSERT(crypto_sign_bytes() == crypto_sign_ed25519_bytes());
        TEST_ASSERT(crypto_sign_seedbytes() == crypto_sign_ed25519_seedbytes());
        TEST_ASSERT(crypto_sign_messagebytes_max() == crypto_sign_ed25519_messagebytes_max());
        TEST_ASSERT(crypto_sign_publickeybytes()
               == crypto_sign_ed25519_publickeybytes());
        TEST_ASSERT(crypto_sign_secretkeybytes()
               == crypto_sign_ed25519_secretkeybytes());
        TEST_ASSERT(crypto_sign_statebytes() == crypto_sign_ed25519ph_statebytes());

    #ifdef ED25519_NONDETERMINISTIC
        exit(0);
    #endif

        return 0;
    }

void _libsodium_ut_sign()
    {
        test();
    }
} // namespace
