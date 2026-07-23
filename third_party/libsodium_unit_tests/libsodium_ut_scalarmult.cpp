#include "libsodium_ut_scalarmult.h"

#include "unity.h"
//#include "aether/tele.h"

namespace scalarmult
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
        unsigned char *alicepk =
            (unsigned char *) sodium_malloc(crypto_scalarmult_BYTES);
        unsigned char *bobpk =
            (unsigned char *) sodium_malloc(crypto_scalarmult_BYTES);
        unsigned char *k = (unsigned char *) sodium_malloc(crypto_scalarmult_BYTES);
        int            ret;

        TEST_ASSERT(alicepk != NULL && bobpk != NULL && k != NULL);

        crypto_scalarmult_base(alicepk, alicesk);
        sodium_bin2hex(hex, sizeof hex, alicepk, crypto_scalarmult_BYTES);
        DebugPrint("%s\n", hex);

        crypto_scalarmult_base(bobpk, bobsk);
        sodium_bin2hex(hex, sizeof hex, bobpk, crypto_scalarmult_BYTES);
        DebugPrint("%s\n", hex);

        ret = crypto_scalarmult(k, alicesk, bobpk);
        TEST_ASSERT(ret == 0);
        sodium_bin2hex(hex, sizeof hex, k, crypto_scalarmult_BYTES);
        DebugPrint("%s\n", hex);

        ret = crypto_scalarmult(k, bobsk, alicepk);
        TEST_ASSERT(ret == 0);
        sodium_bin2hex(hex, sizeof hex, k, crypto_scalarmult_BYTES);
        DebugPrint("%s\n", hex);

        alicepk[31] ^= 0x80;
        ret = crypto_scalarmult(k, bobsk, alicepk);
        TEST_ASSERT(ret == 0);
        sodium_bin2hex(hex, sizeof hex, k, crypto_scalarmult_BYTES);
        DebugPrint("%s\n", hex);

        ret = crypto_scalarmult(k, bobsk, small_order_p);
        TEST_ASSERT(ret == -1);

        sodium_free(bobpk);
        sodium_free(alicepk);
        sodium_free(k);

        TEST_ASSERT(crypto_scalarmult_bytes() > 0U);
        TEST_ASSERT(crypto_scalarmult_scalarbytes() > 0U);
        TEST_ASSERT(strcmp(crypto_scalarmult_primitive(), "curve25519") == 0);
        TEST_ASSERT(crypto_scalarmult_bytes() == crypto_scalarmult_curve25519_bytes());
        TEST_ASSERT(crypto_scalarmult_scalarbytes() ==
               crypto_scalarmult_curve25519_scalarbytes());
        TEST_ASSERT(crypto_scalarmult_bytes() == crypto_scalarmult_scalarbytes());

        return 0;
    }

    void _libsodium_ut_scalarmult()
    {
        test();
    }
} // namespace
