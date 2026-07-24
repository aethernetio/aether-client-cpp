#include "libsodium_ut_core6.h"

#include "unity.h"
//#include "aether/tele.h"

namespace core6
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

    static void
    print(unsigned char *x, unsigned char *y)
    {
        int          i;
        unsigned int borrow = 0;

        for (i = 0; i < 4; ++i) {
            unsigned int xi = x[i];
            unsigned int yi = y[i];
            DebugPrint(",0x%02x", 255 & (xi - yi - borrow));
            borrow = (xi < yi + borrow);
        }
    }

    int
    test(void)
    {
        crypto_core_salsa20(out, in, k, c);
        print(out, c);
        print(out + 20, c + 4);
        DebugPrint("\n");
        print(out + 40, c + 8);
        print(out + 60, c + 12);
        DebugPrint("\n");
        print(out + 24, in);
        print(out + 28, in + 4);
        DebugPrint("\n");
        print(out + 32, in + 8);
        print(out + 36, in + 12);
        DebugPrint("\n");

        return 0;
    }

    void _libsodium_ut_core6()
    {
        test();
    }
} // namespace core6
