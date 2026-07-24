#include "libsodium_ut_auth3.h"
/* "Test Case AUTH256-4" from RFC 4868 */

#include "unity.h"
//#include "aether/tele.h"

namespace auth3
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
        static unsigned char a2[crypto_auth_hmacsha256_BYTES];

        DebugPrint("%d\n", crypto_auth_hmacsha256_verify(a, c, sizeof c, key));

        crypto_auth_hmacsha256(a2, guard_page, 0U, key);
        TEST_ASSERT(crypto_auth_hmacsha256_verify(a2, guard_page, 0U, key) == 0);

        return 0;
    }

    void _libsodium_ut_auth3(void)
    {
        test();
    }
} // namespace auth3
