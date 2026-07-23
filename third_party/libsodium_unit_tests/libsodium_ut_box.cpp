#include "libsodium_ut_box.h"

#include "unity.h"
//#include "aether/tele.h"

namespace box
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
        unsigned char k[crypto_box_BEFORENMBYTES];
        int           i;
        int           ret;

        ret = crypto_box(c, m, 163, nonce, bobpk, alicesk);
        TEST_ASSERT(ret == 0);
        for (i = 16; i < 163; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
            if (i % 8 == 7)
                DebugPrint("\n");
        }
        DebugPrint("\n");

        ret = crypto_box(c, m, 163, nonce, small_order_p, alicesk);
        TEST_ASSERT(ret == -1);

        memset(c, 0, sizeof c);

        ret = crypto_box_beforenm(k, bobpk, alicesk);
        TEST_ASSERT(ret == 0);
        crypto_box_afternm(c, m, 163, nonce, k);
        for (i = 16; i < 163; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
            if (i % 8 == 7)
                DebugPrint("\n");
        }
        DebugPrint("\n");

        ret = crypto_box_beforenm(k, small_order_p, alicesk);
        TEST_ASSERT(ret == -1);

        TEST_ASSERT(crypto_box_seedbytes() > 0U);
        TEST_ASSERT(crypto_box_publickeybytes() > 0U);
        TEST_ASSERT(crypto_box_secretkeybytes() > 0U);
        TEST_ASSERT(crypto_box_beforenmbytes() > 0U);
        TEST_ASSERT(crypto_box_noncebytes() > 0U);
        TEST_ASSERT(crypto_box_zerobytes() > 0U);
        TEST_ASSERT(crypto_box_boxzerobytes() > 0U);
        TEST_ASSERT(crypto_box_macbytes() > 0U);
        TEST_ASSERT(crypto_box_messagebytes_max() > 0U);
        TEST_ASSERT(strcmp(crypto_box_primitive(), "curve25519xsalsa20poly1305") == 0);
        TEST_ASSERT(crypto_box_curve25519xsalsa20poly1305_seedbytes() ==
               crypto_box_seedbytes());
        TEST_ASSERT(crypto_box_curve25519xsalsa20poly1305_publickeybytes() ==
               crypto_box_publickeybytes());
        TEST_ASSERT(crypto_box_curve25519xsalsa20poly1305_secretkeybytes() ==
               crypto_box_secretkeybytes());
        TEST_ASSERT(crypto_box_curve25519xsalsa20poly1305_beforenmbytes() ==
               crypto_box_beforenmbytes());
        TEST_ASSERT(crypto_box_curve25519xsalsa20poly1305_noncebytes() ==
               crypto_box_noncebytes());
        TEST_ASSERT(crypto_box_curve25519xsalsa20poly1305_zerobytes() ==
               crypto_box_zerobytes());
        TEST_ASSERT(crypto_box_curve25519xsalsa20poly1305_boxzerobytes() ==
               crypto_box_boxzerobytes());
        TEST_ASSERT(crypto_box_curve25519xsalsa20poly1305_macbytes() ==
               crypto_box_macbytes());
        TEST_ASSERT(crypto_box_curve25519xsalsa20poly1305_messagebytes_max() ==
               crypto_box_messagebytes_max());

        return 0;
    }

    void _libsodium_ut_box()
    {
        test();
    }
} // namespace box
