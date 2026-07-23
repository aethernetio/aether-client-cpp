#include "libsodium_ut_scalarmult7.h"

#include "unity.h"
//#include "aether/tele.h"

namespace scalarmult7
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
        int ret;

        scalar[0] = 1U;
        ret       = crypto_scalarmult_curve25519(out1, scalar, p1);
        TEST_ASSERT(ret == 0);
        ret = crypto_scalarmult_curve25519(out2, scalar, p2);
        TEST_ASSERT(ret == 0);
        DebugPrint("%d\n", !!memcmp(out1, out2, 32));

        return 0;
    }

    void _libsodium_ut_scalarmult7()
    {
        test();
    }
} // namespace
