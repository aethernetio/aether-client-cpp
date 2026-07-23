#include "libsodium_ut_secretbox2.h"

#include "unity.h"
//#include "aether/tele.h"

namespace secretbox2
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
        int i;

        if (crypto_secretbox_open(m, c, 163, nonce, firstkey) == 0) {
            for (i = 32; i < 163; ++i) {
                DebugPrint(",0x%02x", (unsigned int) m[i]);
                if (i % 8 == 7)
                    DebugPrint("\n");
            }
            DebugPrint("\n");
        }
        TEST_ASSERT(crypto_secretbox_open(m, c, 31, nonce, firstkey) == -1);
        TEST_ASSERT(crypto_secretbox_open(m, c, 16, nonce, firstkey) == -1);
        TEST_ASSERT(crypto_secretbox_open(m, c, 1, nonce, firstkey) == -1);
        TEST_ASSERT(crypto_secretbox_open(m, c, 0, nonce, firstkey) == -1);

        return 0;
    }

    void _libsodium_ut_secretbox2()
    {
        test();
    }
} // namespace
