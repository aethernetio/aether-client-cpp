#include "libsodium_ut_sodium_utils.h"

#include "unity.h"
//#include "aether/tele.h"

namespace sodium_utils
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
        unsigned char  buf_add[1000];
        unsigned char  buf1[1000];
        unsigned char  buf2[1000];
        unsigned char  buf1_rev[1000];
        unsigned char  buf2_rev[1000];
        unsigned char  nonce[24];
        char           nonce_hex[49];
        unsigned char *bin_padded;
        size_t         bin_len, bin_len2;
        size_t         bin_padded_len;
        size_t         bin_padded_maxlen;
        size_t         blocksize;
        unsigned int   i;
        unsigned int   j;

        randombytes_buf(buf1, sizeof buf1);
        memcpy(buf2, buf1, sizeof buf2);
        DebugPrint("%d\n", sodium_memcmp(buf1, buf2, sizeof buf1));
        sodium_memzero(buf1, 0U);
        DebugPrint("%d\n", sodium_memcmp(buf1, buf2, sizeof buf1));
        sodium_memzero(buf1, sizeof buf1 / 2);
        DebugPrint("%d\n", sodium_memcmp(buf1, buf2, sizeof buf1));
        DebugPrint("%d\n", sodium_memcmp(buf1, buf2, 0U));
        sodium_memzero(buf2, sizeof buf2 / 2);
        DebugPrint("%d\n", sodium_memcmp(buf1, buf2, sizeof buf1));
        DebugPrint("%d\n", sodium_memcmp(buf1, guard_page, 0U));
        DebugPrint("%d\n", sodium_memcmp(guard_page, buf2, 0U));
        DebugPrint("%d\n", sodium_memcmp(guard_page, guard_page, 0U));
        sodium_memzero(guard_page, 0U);

        memset(nonce, 0, sizeof nonce);
        sodium_increment(nonce, sizeof nonce);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        memset(nonce, 255, sizeof nonce);
        sodium_increment(nonce, sizeof nonce);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        nonce[1] = 1U;
        sodium_increment(nonce, sizeof nonce);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        nonce[1] = 0U;
        sodium_increment(nonce, sizeof nonce);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        nonce[0] = 255U;
        nonce[2] = 255U;
        sodium_increment(nonce, sizeof nonce);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        for (i = 0U; i < 1000U; i++) {
            bin_len = (size_t) randombytes_uniform(sizeof buf1);
            randombytes_buf(buf1, bin_len);
            randombytes_buf(buf2, bin_len);
            for (j = 0U; j < bin_len; j++) {
                buf1_rev[bin_len - 1 - j] = buf1[j];
                buf2_rev[bin_len - 1 - j] = buf2[j];
            }
            if (memcmp(buf1_rev, buf2_rev, bin_len) *
                sodium_compare(buf1, buf2, bin_len) < 0) {
                DebugPrint("sodium_compare() failure with length=%u\n",
                       (unsigned int) bin_len);
            }
            memcpy(buf1, buf2, bin_len);
            if (sodium_compare(buf1, buf2, bin_len)) {
                DebugPrint("sodium_compare() equality failure with length=%u\n",
                       (unsigned int) bin_len);
            }
        }
        DebugPrint("%d\n", sodium_compare(buf1, NULL, 0U));
        DebugPrint("%d\n", sodium_compare(NULL, buf1, 0U));
        memset(buf1, 0, sizeof buf1);
        if (sodium_is_zero(buf1, sizeof buf1) != 1) {
            DebugPrint("sodium_is_zero() failed\n");
        }
        for (i = 0U; i < sizeof buf1; i++) {
            buf1[i]++;
            if (sodium_is_zero(buf1, sizeof buf1) != 0) {
                DebugPrint("sodium_is_zero() failed\n");
            }
            buf1[i]--;
        }
        bin_len = randombytes_uniform(sizeof buf1);
        randombytes_buf(buf1, bin_len);
        memcpy(buf2, buf1, bin_len);
        memset(buf_add, 0, bin_len);
        j = randombytes_uniform(10000);
        for (i = 0U; i < j; i++) {
            sodium_increment(buf1, bin_len);
            sodium_increment(buf_add, bin_len);
        }
        sodium_add(buf2, buf_add, bin_len);
        if (sodium_compare(buf1, buf2, bin_len) != 0) {
            DebugPrint("sodium_add() failed\n");
        }
        bin_len = randombytes_uniform(sizeof buf1);
        randombytes_buf(buf1, bin_len);
        memcpy(buf2, buf1, bin_len);
        memset(buf_add, 0xff, bin_len);
        sodium_increment(buf2, bin_len);
        sodium_increment(buf2, 0U);
        sodium_add(buf2, buf_add, bin_len);
        sodium_add(buf2, buf_add, 0U);
        if (sodium_compare(buf1, buf2, bin_len) != 0) {
            DebugPrint("sodium_add() failed\n");
        }
        for (i = 0U; i < 1000U; i++) {
            randombytes_buf(buf1, bin_len);
            randombytes_buf(buf2, bin_len);
            sodium_add(buf1, buf2, bin_len);
            sodium_sub(buf1, buf2, bin_len);
            sodium_sub(buf1, buf2, 0U);
            if (sodium_is_zero(buf1, bin_len) &&
                !sodium_is_zero(buf1, bin_len)) {
                DebugPrint("sodium_sub() failed\n");
            }
            sodium_sub(buf1, buf1, bin_len);
            if (!sodium_is_zero(buf1, bin_len)) {
                DebugPrint("sodium_sub() failed\n");
            }
        }
        TEST_ASSERT(sizeof nonce >= 24U);
        memset(nonce, 0xfe, 24U);
        memset(nonce, 0xff, 6U);
        sodium_increment(nonce, 8U);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        memset(nonce, 0xfe, 24U);
        memset(nonce, 0xff, 10U);
        sodium_increment(nonce, 12U);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        memset(nonce, 0xff, 22U);
        sodium_increment(nonce, 24U);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));

        TEST_ASSERT(sizeof nonce >= 24U);
        memset(nonce, 0xfe, 24U);
        memset(nonce, 0xff, 6U);
        sodium_add(nonce, nonce, 7U);
        sodium_add(nonce, nonce, 8U);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        memset(nonce, 0xfe, 24U);
        memset(nonce, 0xff, 10U);
        sodium_add(nonce, nonce, 11U);
        sodium_add(nonce, nonce, 12U);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        memset(nonce, 0xff, 22U);
        sodium_add(nonce, nonce, 23U);
        sodium_add(nonce, nonce, 24U);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        sodium_add(nonce, nonce, 0U);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        sodium_add(nonce, guard_page, 0U);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        sodium_add(guard_page, nonce, 0U);

        sodium_sub(nonce, nonce, 0U);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        sodium_sub(nonce, guard_page, 0U);
        DebugPrint("%s\n",
               sodium_bin2hex(nonce_hex, sizeof nonce_hex, nonce, sizeof nonce));
        sodium_sub(guard_page, nonce, 0U);

        randombytes_buf(buf1, 64U);
        randombytes_buf(buf2, 64U);
        memset(buf_add, 0, 64U);
        sodium_add(buf_add, buf1, 64U);
        TEST_ASSERT(!sodium_is_zero(buf_add, 64U));
        sodium_add(buf_add, buf2, 64U);
        TEST_ASSERT(!sodium_is_zero(buf_add, 64U));
        sodium_sub(buf_add, buf1, 64U);
        TEST_ASSERT(!sodium_is_zero(buf_add, 64U));
        sodium_sub(buf_add, buf2, 64U);
        TEST_ASSERT(sodium_is_zero(buf_add, 64U));

        for (i = 0; i < 2000U; i++) {
            bin_len = randombytes_uniform(200U);
            blocksize = 1U + randombytes_uniform(500U);
            bin_padded_maxlen = bin_len + (blocksize - bin_len % blocksize);
            bin_padded = (unsigned char *) sodium_malloc(bin_padded_maxlen);
            randombytes_buf(bin_padded, bin_padded_maxlen);

            TEST_ASSERT(sodium_pad(&bin_padded_len, bin_padded, bin_len,
                              blocksize, bin_padded_maxlen - 1U) == -1);
            TEST_ASSERT(sodium_pad(NULL, bin_padded, bin_len,
                              blocksize, bin_padded_maxlen + 1U) == 0);
            TEST_ASSERT(sodium_pad(&bin_padded_len, bin_padded, bin_len,
                              blocksize, bin_padded_maxlen + 1U) == 0);
            TEST_ASSERT(sodium_pad(&bin_padded_len, bin_padded, bin_len,
                              0U, bin_padded_maxlen) == -1);
            TEST_ASSERT(sodium_pad(&bin_padded_len, bin_padded, bin_len,
                              blocksize, bin_padded_maxlen) == 0);
            TEST_ASSERT(bin_padded_len == bin_padded_maxlen);

            TEST_ASSERT(sodium_unpad(&bin_len2, bin_padded, bin_padded_len,
                                bin_padded_len + 1U) == -1);
            TEST_ASSERT(sodium_unpad(&bin_len2, bin_padded, bin_padded_len,
                                0U) == -1);
            TEST_ASSERT(sodium_unpad(&bin_len2, bin_padded, bin_padded_len,
                                blocksize) == 0);
            TEST_ASSERT(bin_len2 == bin_len);

            sodium_free(bin_padded);
        }

        sodium_stackzero(512);

        return 0;
    }

    void _libsodium_ut_sodium_utils()
    {
        test();
    }
} // namespace
