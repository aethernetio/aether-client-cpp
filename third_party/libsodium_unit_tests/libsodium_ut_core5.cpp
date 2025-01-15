#include "libsodium_ut_core5.h"

#include "unity.h"
//#include "aether/tele/tele.h"

namespace core5
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

        crypto_core_hsalsa20(out, in, k, c);
        for (i = 0; i < 32; ++i) {
            DebugPrint(",0x%02x", (unsigned int) out[i]);
            if (i % 8 == 7) {
                DebugPrint("\n");
            }
        }
        return 0;
    }

    void _libsodium_ut_core5()
    {
        test();
    }
} // namespace core5
