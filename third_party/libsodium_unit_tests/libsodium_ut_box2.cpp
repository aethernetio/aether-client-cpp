#include "libsodium_ut_box2.h"

#include "unity.h"
//#include "aether/tele.h"

namespace box2
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

        if (crypto_box_open(m, c, 163, nonce, alicepk, bobsk) == 0) {
            for (i = 32; i < 163; ++i) {
                DebugPrint(",0x%02x", (unsigned int) m[i]);
                if (i % 8 == 7)
                    DebugPrint("\n");
            }
            DebugPrint("\n");
        }
        ret = crypto_box_open(m, c, 163, nonce, small_order_p, bobsk);
        TEST_ASSERT(ret == -1);

        memset(m, 0, sizeof m);
        ret = crypto_box_beforenm(k, small_order_p, bobsk);
        TEST_ASSERT(ret == -1);
        ret = crypto_box_beforenm(k, alicepk, bobsk);
        TEST_ASSERT(ret == 0);
        if (crypto_box_open_afternm(m, c, 163, nonce, k) == 0) {
            for (i = 32; i < 163; ++i) {
                DebugPrint(",0x%02x", (unsigned int) m[i]);
                if (i % 8 == 7)
                    DebugPrint("\n");
            }
            DebugPrint("\n");
        }
        return 0;
    }

    void _libsodium_ut_box2()
    {
        test();
    }
} // namespace box2
