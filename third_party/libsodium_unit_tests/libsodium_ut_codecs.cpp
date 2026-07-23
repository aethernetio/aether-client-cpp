#include "libsodium_ut_codecs.h"

#include "unity.h"
//#include "aether/tele.h"

namespace codecs
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
        unsigned char  buf1[1000];
        char           buf3[33];
        unsigned char  buf4[4];
        const char    *b64;
        char          *b64_;
        const char    *b64_end;
        unsigned char *bin;
        const char    *hex;
        const char    *hex_end;
        size_t         b64_len;
        size_t         bin_len;
        unsigned int   i;

        DebugPrint("%s\n",
               sodium_bin2hex(buf3, 33U, (const unsigned char *) "0123456789ABCDEF",
                              16U));
        DebugPrint("bin2hex(..., guard_page, 0):%s\n",
               sodium_bin2hex(buf3, sizeof buf3, guard_page, 0U));
        DebugPrint("bin2hex(..., \"\", 0):%s\n",
               sodium_bin2hex(buf3, sizeof buf3, (const unsigned char *) "", 0U));

        hex = "Cafe : 6942";
        sodium_hex2bin(buf4, sizeof buf4, hex, strlen(hex), ": ", &bin_len,
                       &hex_end);
        DebugPrint("%lu:%02x%02x%02x%02x\n", (unsigned long) bin_len,
               buf4[0], buf4[1], buf4[2], buf4[3]);
        DebugPrint("dt1: %ld\n", (long) (hex_end - hex));

        hex = "Cafe : 6942";
        sodium_hex2bin(buf4, sizeof buf4, hex, strlen(hex), ": ", &bin_len, NULL);
        DebugPrint("%lu:%02x%02x%02x%02x\n", (unsigned long) bin_len,
               buf4[0], buf4[1], buf4[2], buf4[3]);

        hex = "deadbeef";
        if (sodium_hex2bin(buf1, 1U, hex, 8U, NULL, &bin_len, &hex_end) != -1) {
            DebugPrint("sodium_hex2bin() overflow not detected\n");
        }
        DebugPrint("dt2: %ld\n", (long) (hex_end - hex));

        hex = "de:ad:be:eff";
        if (sodium_hex2bin(buf1, 4U, hex, 12U, ":", &bin_len, &hex_end) != -1) {
            DebugPrint(
                "sodium_hex2bin() with an odd input length and a short output "
                "buffer\n");
        }
        DebugPrint("dt3: %ld\n", (long) (hex_end - hex));

        hex = "de:ad:be:eff";
        if (sodium_hex2bin(buf1, sizeof buf1, hex, 12U, ":",
                           &bin_len, &hex_end) != -1) {
            DebugPrint("sodium_hex2bin() with an odd input length\n");
        }
        DebugPrint("dt4: %ld\n", (long) (hex_end - hex));

        hex = "de:ad:be:eff";
        if (sodium_hex2bin(buf1, sizeof buf1, hex, 13U, ":",
                           &bin_len, &hex_end) != -1) {
            DebugPrint("sodium_hex2bin() with an odd input length (2)\n");
        }
        DebugPrint("dt5: %ld\n", (long) (hex_end - hex));

        hex = "de:ad:be:eff";
        if (sodium_hex2bin(buf1, sizeof buf1, hex, 12U, ":",
                           &bin_len, NULL) != -1) {
            DebugPrint("sodium_hex2bin() with an odd input length and no end pointer\n");
        }

        hex = "de:ad:be:ef*";
        if (sodium_hex2bin(buf1, sizeof buf1, hex, 12U, ":",
                           &bin_len, &hex_end) != 0) {
            DebugPrint("sodium_hex2bin() with an extra character and an end pointer\n");
        }
        DebugPrint("dt6: %ld\n", (long) (hex_end - hex));

        hex = "de:ad:be:ef*";
        if (sodium_hex2bin(buf1, sizeof buf1, hex, 12U, ":",
                           &bin_len, NULL) != -1) {
            DebugPrint("sodium_hex2bin() with an extra character and no end pointer\n");
        }

        TEST_ASSERT(sodium_hex2bin(buf4, sizeof buf4, (const char *) guard_page, 0U,
                              NULL, &bin_len, NULL) == 0);
        TEST_ASSERT(bin_len == 0);

        TEST_ASSERT(sodium_hex2bin(buf4, sizeof buf4, "", 0U, NULL, &bin_len, NULL) == 0);
        TEST_ASSERT(bin_len == 0);

        DebugPrint("%s\n",
               sodium_bin2base64(buf3, 31U, (const unsigned char *) "\xfb\xf0\xf1" "0123456789ABCDEFab",
                                 21U, sodium_base64_VARIANT_ORIGINAL));
        DebugPrint("%s\n",
               sodium_bin2base64(buf3, 33U, (const unsigned char *) "\xfb\xf0\xf1" "0123456789ABCDEFabc",
                                 22U, sodium_base64_VARIANT_ORIGINAL_NO_PADDING));
        DebugPrint("%s\n",
               sodium_bin2base64(buf3, 31U, (const unsigned char *) "\xfb\xf0\xf1" "0123456789ABCDEFab",
                                 21U, sodium_base64_VARIANT_URLSAFE));
        DebugPrint("%s\n",
               sodium_bin2base64(buf3, 33U, (const unsigned char *) "\xfb\xf0\xf1" "0123456789ABCDEFabc",
                                 22U, sodium_base64_VARIANT_URLSAFE_NO_PADDING));
        DebugPrint("%s\n",
               sodium_bin2base64(buf3, 1U, guard_page,
                                 0U, sodium_base64_VARIANT_ORIGINAL));
        DebugPrint("%s\n",
               sodium_bin2base64(buf3, 5U, (const unsigned char *) "a",
                                 1U, sodium_base64_VARIANT_ORIGINAL));
        DebugPrint("%s\n",
               sodium_bin2base64(buf3, 5U, (const unsigned char *) "ab",
                                 2U, sodium_base64_VARIANT_ORIGINAL));
        DebugPrint("%s\n",
               sodium_bin2base64(buf3, 5U, (const unsigned char *) "abc",
                                 3U, sodium_base64_VARIANT_ORIGINAL));
        DebugPrint("%s\n",
               sodium_bin2base64(buf3, 1U, guard_page,
                                 0U, sodium_base64_VARIANT_ORIGINAL_NO_PADDING));
        DebugPrint("%s\n",
               sodium_bin2base64(buf3, 3U, (const unsigned char *) "a",
                                 1U, sodium_base64_VARIANT_ORIGINAL_NO_PADDING));
        DebugPrint("%s\n",
               sodium_bin2base64(buf3, 4U, (const unsigned char *) "ab",
                                 2U, sodium_base64_VARIANT_ORIGINAL_NO_PADDING));
        DebugPrint("%s\n",
               sodium_bin2base64(buf3, 5U, (const unsigned char *) "abc",
                                 3U, sodium_base64_VARIANT_ORIGINAL_NO_PADDING));

        b64 = "VGhpcyBpcyBhIGpvdXJu" "\n" "ZXkgaW50by" " " "Bzb3VuZA==";
        memset(buf4, '*', sizeof buf4);
        TEST_ASSERT(sodium_base642bin(buf4, sizeof buf4, b64, strlen(b64), "\n\r ", &bin_len,
                                 &b64_end, sodium_base64_VARIANT_ORIGINAL) == -1);
        buf4[bin_len] = 0;
        DebugPrint("[%s]\n", (const char *) buf4);
        DebugPrint("[%s]\n", b64_end);

        memset(buf1, '*', sizeof buf1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), "\n\r ", &bin_len,
                                 &b64_end, sodium_base64_VARIANT_ORIGINAL) == 0);
        buf1[bin_len] = 0;
        DebugPrint("[%s]\n", (const char *) buf1);
        TEST_ASSERT(*b64_end == 0);

        memset(buf1, '*', sizeof buf1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, &bin_len,
                                 &b64_end, sodium_base64_VARIANT_ORIGINAL) == 0);
        buf1[bin_len] = 0;
        DebugPrint("[%s]\n", (const char *) buf1);
        DebugPrint("[%s]\n", b64_end);

        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, NULL,
                                 &b64_end, sodium_base64_VARIANT_ORIGINAL) == 0);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, NULL,
                                 &b64_end, sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == 0);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), " \r\n", NULL,
                                 &b64_end, sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == 0);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, NULL,
                                 &b64_end, sodium_base64_VARIANT_URLSAFE_NO_PADDING) == 0);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), " \r\n", NULL,
                                 &b64_end, sodium_base64_VARIANT_URLSAFE_NO_PADDING) == 0);

        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, NULL,
                                 NULL, sodium_base64_VARIANT_ORIGINAL) == -1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, NULL,
                                 NULL, sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == -1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), " \r\n", NULL,
                                 NULL, sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == -1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), NULL, NULL,
                                 NULL, sodium_base64_VARIANT_URLSAFE_NO_PADDING) == -1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, b64, strlen(b64), " \r\n", NULL,
                                 NULL, sodium_base64_VARIANT_URLSAFE_NO_PADDING) == -1);

        TEST_ASSERT(sodium_base642bin(guard_page, (size_t) 10U, "a=", (size_t) 2U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_URLSAFE) == -1);
        TEST_ASSERT(sodium_base642bin(guard_page, (size_t) 10U, "a*", (size_t) 2U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_URLSAFE) == -1);
        TEST_ASSERT(sodium_base642bin(guard_page, (size_t) 10U, "a*", (size_t) 2U, "~", NULL, NULL,
                                 sodium_base64_VARIANT_URLSAFE) == -1);
        TEST_ASSERT(sodium_base642bin(guard_page, (size_t) 10U, "a*", (size_t) 2U, "*", NULL, NULL,
                                 sodium_base64_VARIANT_URLSAFE) == -1);
        TEST_ASSERT(sodium_base642bin(guard_page, (size_t) 10U, "a==", (size_t) 3U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_URLSAFE) == -1);
        TEST_ASSERT(sodium_base642bin(guard_page, (size_t) 10U, "a=*", (size_t) 3U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_URLSAFE) == -1);
        TEST_ASSERT(sodium_base642bin(guard_page, (size_t) 10U, "a=*", (size_t) 3U, "~", NULL, NULL,
                                 sodium_base64_VARIANT_URLSAFE) == -1);
        TEST_ASSERT(sodium_base642bin(guard_page, (size_t) 10U, "a=*", (size_t) 3U, "*", NULL, NULL,
                                 sodium_base64_VARIANT_URLSAFE) == -1);

        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "O1R", (size_t) 3U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == -1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "O1Q", (size_t) 3U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == 0);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "O1", (size_t) 2U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == -1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "Ow", (size_t) 2U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == 0);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "O", (size_t) 1U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == -1);

        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "", (size_t) 0U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL) == 0);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "A", (size_t) 1U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL) == -1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "AA", (size_t) 2U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL) == -1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "kaw", (size_t) 3U, NULL, NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL) == -1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "kQ*", (size_t) 3U, "@", NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL) == -1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "kQ*", (size_t) 3U, "*", NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL) == -1);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "kaw=**", (size_t) 6U, "*", NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL) == 0);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "kaw*=*", (size_t) 6U, "~*", NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL) == 0);
        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "ka*w*=*", (size_t) 7U, "*~", NULL, NULL,
                                 sodium_base64_VARIANT_ORIGINAL) == 0);

        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, (const char *) guard_page, 0U,
                                 NULL, &bin_len, NULL, sodium_base64_VARIANT_ORIGINAL) == 0);
        TEST_ASSERT(bin_len == 0);

        TEST_ASSERT(sodium_base642bin(buf1, sizeof buf1, "", 0U, NULL, &bin_len, NULL,
                                 sodium_base64_VARIANT_ORIGINAL) == 0);
        TEST_ASSERT(bin_len == 0);

        for (i = 0; i < 1000; i++) {
            TEST_ASSERT(sizeof buf1 >= 100);
            bin_len = (size_t) randombytes_uniform(100);
            bin = (unsigned char *) sodium_malloc(bin_len);
            b64_len = (bin_len + 2U) / 3U * 4U + 1U;
            TEST_ASSERT(b64_len == sodium_base64_encoded_len(bin_len, sodium_base64_VARIANT_URLSAFE));
            b64_ = (char *) sodium_malloc(b64_len);
            randombytes_buf(bin, bin_len);
            memcpy(buf1, bin, bin_len);
            b64 = sodium_bin2base64(b64_, b64_len, bin, bin_len,
                                    sodium_base64_VARIANT_URLSAFE);
            TEST_ASSERT(b64 != NULL);
            TEST_ASSERT(sodium_base642bin(bin, bin_len + 10, b64, b64_len,
                                     NULL, NULL, &b64_end,
                                     sodium_base64_VARIANT_URLSAFE) == 0);
            TEST_ASSERT(b64_end == &b64[b64_len - 1]);
            TEST_ASSERT(memcmp(bin, buf1, bin_len) == 0);
            sodium_free(bin);
            sodium_free(b64_);
        }
        return 0;
    }

    void _libsodium_ut_codecs()
    {
        test();
    }
} // namespace codecs
