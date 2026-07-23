#include "libsodium_ut_ed25519_convert.h"

#include "unity.h"
//#include "aether/tele.h"

namespace ed25519_convert
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
        unsigned char ed25519_pk[crypto_sign_ed25519_PUBLICKEYBYTES];
        unsigned char ed25519_skpk[crypto_sign_ed25519_SECRETKEYBYTES];
        unsigned char curve25519_pk[crypto_scalarmult_curve25519_BYTES];
        unsigned char curve25519_pk2[crypto_scalarmult_curve25519_BYTES];
        unsigned char curve25519_sk[crypto_scalarmult_curve25519_BYTES];
        char          curve25519_pk_hex[crypto_scalarmult_curve25519_BYTES * 2 + 1];
        char          curve25519_sk_hex[crypto_scalarmult_curve25519_BYTES * 2 + 1];
        unsigned int  i;

        TEST_ASSERT(crypto_sign_ed25519_SEEDBYTES <= crypto_hash_sha512_BYTES);
        crypto_sign_ed25519_seed_keypair(ed25519_pk, ed25519_skpk, keypair_seed);

        if (crypto_sign_ed25519_pk_to_curve25519(curve25519_pk, ed25519_pk) != 0) {
            DebugPrint("conversion failed\n");
        }
        crypto_sign_ed25519_sk_to_curve25519(curve25519_sk, ed25519_skpk);
        sodium_bin2hex(curve25519_pk_hex, sizeof curve25519_pk_hex, curve25519_pk,
                       sizeof curve25519_pk);
        sodium_bin2hex(curve25519_sk_hex, sizeof curve25519_sk_hex, curve25519_sk,
                       sizeof curve25519_sk);

        DebugPrint("curve25519 pk: [%s]\n", curve25519_pk_hex);
        DebugPrint("curve25519 sk: [%s]\n", curve25519_sk_hex);

        for (i = 0U; i < 500U; i++) {
            crypto_sign_ed25519_keypair(ed25519_pk, ed25519_skpk);
            if (crypto_sign_ed25519_pk_to_curve25519(curve25519_pk, ed25519_pk) !=
                0) {
                DebugPrint("conversion failed\n");
            }
            crypto_sign_ed25519_sk_to_curve25519(curve25519_sk, ed25519_skpk);
            crypto_scalarmult_curve25519_base(curve25519_pk2, curve25519_sk);
            if (memcmp(curve25519_pk, curve25519_pk2, sizeof curve25519_pk) != 0) {
                DebugPrint("conversion failed\n");
            }
        }

        sodium_hex2bin(ed25519_pk, crypto_sign_ed25519_PUBLICKEYBYTES,
                       "0000000000000000000000000000000000000000000000000000000000000000"
                       "0000000000000000000000000000000000000000000000000000000000000000",
                       64, NULL, NULL, NULL);
        TEST_ASSERT(crypto_sign_ed25519_pk_to_curve25519(curve25519_pk, ed25519_pk) == -1);
        sodium_hex2bin(ed25519_pk, crypto_sign_ed25519_PUBLICKEYBYTES,
                       "0200000000000000000000000000000000000000000000000000000000000000"
                       "0000000000000000000000000000000000000000000000000000000000000000",
                       64, NULL, NULL, NULL);
        TEST_ASSERT(crypto_sign_ed25519_pk_to_curve25519(curve25519_pk, ed25519_pk) == -1);
        sodium_hex2bin(ed25519_pk, crypto_sign_ed25519_PUBLICKEYBYTES,
                       "0500000000000000000000000000000000000000000000000000000000000000"
                       "0000000000000000000000000000000000000000000000000000000000000000",
                       64, NULL, NULL, NULL);
        TEST_ASSERT(crypto_sign_ed25519_pk_to_curve25519(curve25519_pk, ed25519_pk) == -1);

        DebugPrint("ok\n");

        return 0;
    }

    void _libsodium_ut_ed25519_convert()
    {
        test();
    }
} // namespace
