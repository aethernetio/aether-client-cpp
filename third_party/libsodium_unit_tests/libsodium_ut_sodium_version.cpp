#include "libsodium_ut_sodium_version.h"

#include "unity.h"
//#include "aether/tele.h"

namespace sodium_version
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
        DebugPrint("%d\n", sodium_version_string() != NULL);
        DebugPrint("%d\n", sodium_library_version_major() > 0);
        DebugPrint("%d\n", sodium_library_version_minor() >= 0);
    #ifdef SODIUM_LIBRARY_MINIMAL
        TEST_ASSERT(sodium_library_minimal() == 1);
    #else
        TEST_ASSERT(sodium_library_minimal() == 0);
    #endif

        return 0;
    }

    void _libsodium_ut_sodium_version()
    {
        test();
    }
} // namespace
