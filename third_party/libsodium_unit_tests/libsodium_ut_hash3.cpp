#include "libsodium_ut_hash3.h"

#include "unity.h"
//#include "aether/tele.h"

namespace hash3
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

        crypto_hash(h, x, sizeof x - 1U);
        for (i = 0; i < crypto_hash_BYTES; ++i) {
            DebugPrint("%02x", (unsigned int) h[i]);
        }
        DebugPrint("\n");

        return 0;
    }

    void _libsodium_ut_hash3()
    {
        test();
    }
} // namespace
