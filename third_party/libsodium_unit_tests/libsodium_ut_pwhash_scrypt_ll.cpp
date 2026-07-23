#include "libsodium_ut_pwhash_scrypt_ll.h"

#include "unity.h"
//#include "aether/tele.h"

namespace pwhash_scrypt_ll
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
    tv(const char *passwd, const char *salt, uint64_t N, uint32_t r, uint32_t p)
    {
        uint8_t data[64];
        size_t  i;
        size_t  olen       = (sizeof data / sizeof data[0]);
        size_t  passwd_len = strlen(passwd);
        size_t  salt_len   = strlen(salt);
        int     line_items  = 0;

        if (crypto_pwhash_scryptsalsa208sha256_ll(
                (const uint8_t *) passwd, passwd_len, (const uint8_t *) salt,
                salt_len, N, r, p, data, olen) != 0) {
            DebugPrint("pwhash_scryptsalsa208sha256_ll([%s],[%s]) failure\n", passwd,
                   salt);
            return;
        }

        DebugPrint("scrypt('%s', '%s', %lu, %lu, %lu, %lu) =\n", passwd, salt,
               (unsigned long) N, (unsigned long) r, (unsigned long) p,
               (unsigned long) olen);

        for (i = 0; i < olen; i++) {
            DebugPrint("%02x%c", data[i], line_items < 15 ? ' ' : '\n');
            line_items = line_items < 15 ? line_items + 1 : 0;
        }
    }

    int
    test(void)
    {
        tv(passwd1, salt1, N1, r1, p1);
        tv(passwd2, salt2, N2, r2, p2);
        tv(passwd3, salt3, N3, r3, p3);

        return 0;
    }

    void _libsodium_ut_pwhash_scrypt_ll()
    {
        test();
    }
} // namespace
