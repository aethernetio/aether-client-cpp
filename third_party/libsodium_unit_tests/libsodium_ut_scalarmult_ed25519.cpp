#include "libsodium_ut_scalarmult_ed25519.h"

#include "unity.h"
//#include "aether/tele.h"

namespace scalarmult_ed25519
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
        unsigned char *n, *p, *q, *q2;

        n = (unsigned char *) sodium_malloc(crypto_scalarmult_ed25519_SCALARBYTES);
        p = (unsigned char *) sodium_malloc(crypto_scalarmult_ed25519_BYTES);
        q = (unsigned char *) sodium_malloc(crypto_scalarmult_ed25519_BYTES);
        q2 = (unsigned char *) sodium_malloc(crypto_scalarmult_ed25519_BYTES);

        randombytes_buf(n, crypto_scalarmult_ed25519_SCALARBYTES);
        if (crypto_scalarmult_ed25519_base(q, n) != 0) {
            DebugPrint("crypto_scalarmult_ed25519_base() failed\n");
        }
        memcpy(p, B, crypto_scalarmult_ed25519_BYTES);
        if (crypto_scalarmult_ed25519(q2, n, p) != 0) {
            DebugPrint("crypto_scalarmult_ed25519() failed\n");
        }
        if (memcmp(q, q2, crypto_scalarmult_ed25519_BYTES) != 0) {
            DebugPrint("crypto_scalarmult_ed25519_base(n) != crypto_scalarmult_ed25519(n, 9)\n");
        }

        memset(n, 0, crypto_scalarmult_ed25519_SCALARBYTES);
        if (crypto_scalarmult_ed25519_base(q, n) != -1) {
            DebugPrint("crypto_scalarmult_ed25519_base(0) passed\n");
        }
        if (crypto_scalarmult_ed25519(q2, n, p) != -1) {
            DebugPrint("crypto_scalarmult_ed25519(0) passed\n");
        }
        if (crypto_scalarmult_ed25519_noclamp(q2, n, p) != -1) {
            DebugPrint("crypto_scalarmult_ed25519_noclamp(0) passed\n");
        }

        n[0] = 1;
        if (crypto_scalarmult_ed25519_base(q, n) != 0) {
            DebugPrint("crypto_scalarmult_ed25519_base() failed\n");
        }
        if (crypto_scalarmult_ed25519(q2, n, p) != 0) {
            DebugPrint("crypto_scalarmult_ed25519() failed\n");
        }
        if (crypto_scalarmult_ed25519_noclamp(q2, n, p) != 0) {
            DebugPrint("crypto_scalarmult_ed25519_noclamp() failed\n");
        }

        if (crypto_scalarmult_ed25519(q, n, non_canonical_p) != -1) {
            DebugPrint("crypto_scalarmult_ed25519() didn't fail\n");
        }
        if (crypto_scalarmult_ed25519(q, n, non_canonical_invalid_p) != -1) {
            DebugPrint("crypto_scalarmult_ed25519() didn't fail\n");
        }
        if (crypto_scalarmult_ed25519(q, n, max_canonical_p) != 0) {
            DebugPrint("crypto_scalarmult_ed25519() failed\n");
        }

        n[0] = 9;
        if (crypto_scalarmult_ed25519(q, n, p) != 0) {
            DebugPrint("crypto_scalarmult_ed25519() failed\n");
        }
        if (crypto_scalarmult_ed25519_noclamp(q2, n, p) != 0) {
            DebugPrint("crypto_scalarmult_ed25519_noclamp() failed\n");
        }
        if (memcmp(q, q2, crypto_scalarmult_ed25519_BYTES) == 0) {
            DebugPrint("clamping not applied\n");
        }

        n[0] = 9;
        if (crypto_scalarmult_ed25519_base(q, n) != 0) {
            DebugPrint("crypto_scalarmult_ed25519_base() failed\n");
        }
        if (crypto_scalarmult_ed25519_base_noclamp(q2, n) != 0) {
            DebugPrint("crypto_scalarmult_ed25519_base_noclamp() failed\n");
        }
        if (memcmp(q, q2, crypto_scalarmult_ed25519_BYTES) == 0) {
            DebugPrint("clamping not applied\n");
        }

        n[0] = 8;
        n[31] = 64;
        if (crypto_scalarmult_ed25519_noclamp(q2, n, p) != 0) {
            DebugPrint("crypto_scalarmult_ed25519_noclamp() failed\n");
        }
        if (memcmp(q, q2, crypto_scalarmult_ed25519_BYTES) != 0) {
            DebugPrint("inconsistent clamping\n");
        }

        memset(p, 0, crypto_scalarmult_ed25519_BYTES);
        if (crypto_scalarmult_ed25519(q, n, p) != -1) {
            DebugPrint("crypto_scalarmult_ed25519() didn't fail\n");
        }
        if (crypto_scalarmult_ed25519_noclamp(q, n, p) != -1) {
            DebugPrint("crypto_scalarmult_ed25519_noclamp() didn't fail\n");
        }

        n[0] = 8;
        if (crypto_scalarmult_ed25519(q, n, p) != -1) {
            DebugPrint("crypto_scalarmult_ed25519() didn't fail\n");
        }
        if (crypto_scalarmult_ed25519_noclamp(q, n, p) != -1) {
            DebugPrint("crypto_scalarmult_ed25519_noclamp() didn't fail\n");
        }

        sodium_free(q2);
        sodium_free(q);
        sodium_free(p);
        sodium_free(n);

        TEST_ASSERT(crypto_scalarmult_ed25519_BYTES == crypto_scalarmult_ed25519_bytes());
        TEST_ASSERT(crypto_scalarmult_ed25519_SCALARBYTES == crypto_scalarmult_ed25519_scalarbytes());

        DebugPrint("OK\n");

        return 0;
    }

    void _libsodium_ut_scalarmult_ed25519()
    {
        test();
    }
} // namespace
