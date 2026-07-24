#include "libsodium_ut_box_easy.h"

#include "unity.h"
//#include "aether/tele.h"

namespace box_easy
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
        size_t i;
        int    ret;

        ret = crypto_box_easy(c, m, 131, nonce, bobpk, alicesk);
        TEST_ASSERT(ret == 0);
        for (i = 0; i < 131 + crypto_box_MACBYTES; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
        }
        DebugPrint("\n");

        /* Null message */

        ret = crypto_box_easy(c, guard_page, 0, nonce, bobpk, alicesk);
        TEST_ASSERT(ret == 0);
        for (i = 0; i < 1 + crypto_box_MACBYTES; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
        }
        DebugPrint("\n");

        ret =
            crypto_box_open_easy(c, c, crypto_box_MACBYTES, nonce, bobpk, alicesk);
        TEST_ASSERT(ret == 0);
        for (i = 0; i < 1 + crypto_box_MACBYTES; ++i) {
            DebugPrint(",0x%02x", (unsigned int) c[i]);
        }
        DebugPrint("\n");
        c[randombytes_uniform(crypto_box_MACBYTES)]++;
        ret = crypto_box_open_easy(c, c, crypto_box_MACBYTES, nonce, bobpk, alicesk);
        TEST_ASSERT(ret == -1);

        return 0;
    }

    void _libsodium_ut_box_easy()
    {
        test();
    }
} // namespace box_easy
