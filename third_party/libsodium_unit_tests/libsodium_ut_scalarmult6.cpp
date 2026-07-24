#include "libsodium_ut_scalarmult6.h"

#include "unity.h"
//#include "aether/tele.h"

namespace scalarmult6
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
        unsigned char *k;
        unsigned char *bobsk;
        unsigned char *alicepk;
        int            i;
        int            ret;

        k       = (unsigned char *) sodium_malloc(crypto_scalarmult_BYTES);
        bobsk   = (unsigned char *) sodium_malloc(crypto_scalarmult_SCALARBYTES);
        alicepk = (unsigned char *) sodium_malloc(crypto_scalarmult_SCALARBYTES);
        TEST_ASSERT(k != NULL && bobsk != NULL && alicepk != NULL);

        memcpy(bobsk, bobsk_, crypto_scalarmult_SCALARBYTES);
        memcpy(alicepk, alicepk_, crypto_scalarmult_SCALARBYTES);

        ret = crypto_scalarmult(k, bobsk, alicepk);
        TEST_ASSERT(ret == 0);

        sodium_free(alicepk);
        sodium_free(bobsk);

        for (i = 0; i < 32; ++i) {
            if (i > 0) {
                DebugPrint(",");
            } else {
                DebugPrint(" ");
            }
            DebugPrint("0x%02x", (unsigned int) k[i]);
            if (i % 8 == 7) {
                DebugPrint("\n");
            }
        }
        sodium_free(k);

        return 0;
    }

    void _libsodium_ut_scalarmult6()
    {
        test();
    }
} // namespace
