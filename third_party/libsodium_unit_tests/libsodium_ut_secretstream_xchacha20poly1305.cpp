#include "libsodium_ut_secretstream_xchacha20poly1305.h"

#include "unity.h"
//#include "aether/tele.h"

namespace secretstream_xchacha20poly1305
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
        crypto_secretstream_xchacha20poly1305_state *state, *statesave;
        crypto_secretstream_xchacha20poly1305_state state_copy;
        unsigned char      *ad;
        unsigned char      *header;
        unsigned char      *k;
        unsigned char      *c1, *c2, *c3, *csave;
        unsigned char      *m1, *m2, *m3;
        unsigned char      *m1_, *m2_, *m3_;
        unsigned long long  res_len;
        size_t              ad_len;
        size_t              m1_len, m2_len, m3_len;
        int                 ret;
        unsigned char       tag;

        state = (crypto_secretstream_xchacha20poly1305_state *)
            sodium_malloc(crypto_secretstream_xchacha20poly1305_statebytes());
        statesave = (crypto_secretstream_xchacha20poly1305_state *)
            sodium_malloc(crypto_secretstream_xchacha20poly1305_statebytes());
        header = (unsigned char *)
            sodium_malloc(crypto_secretstream_xchacha20poly1305_HEADERBYTES);

        ad_len = randombytes_uniform(100);
        m1_len = randombytes_uniform(1000);
        m2_len = randombytes_uniform(1000);
        m3_len = randombytes_uniform(1000);

        c1 = (unsigned char *)
            sodium_malloc(m1_len + crypto_secretstream_xchacha20poly1305_ABYTES);
        c2 = (unsigned char *)
            sodium_malloc(m2_len + crypto_secretstream_xchacha20poly1305_ABYTES);
        c3 = (unsigned char *)
            sodium_malloc(m3_len + crypto_secretstream_xchacha20poly1305_ABYTES);
        csave = (unsigned char *)
            sodium_malloc((m1_len | m2_len | m3_len) + crypto_secretstream_xchacha20poly1305_ABYTES);

        ad  = (unsigned char *) sodium_malloc(ad_len);
        m1  = (unsigned char *) sodium_malloc(m1_len);
        m2  = (unsigned char *) sodium_malloc(m2_len);
        m3  = (unsigned char *) sodium_malloc(m3_len);
        m1_ = (unsigned char *) sodium_malloc(m1_len);
        m2_ = (unsigned char *) sodium_malloc(m2_len);
        m3_ = (unsigned char *) sodium_malloc(m3_len);

        randombytes_buf(ad, ad_len);

        randombytes_buf(m1, m1_len);
        memcpy(m1_, m1, m1_len);
        randombytes_buf(m2, m2_len);
        memcpy(m2_, m2, m2_len);
        randombytes_buf(m3, m3_len);
        memcpy(m3_, m3, m3_len);

        k = (unsigned char *)
            sodium_malloc(crypto_secretstream_xchacha20poly1305_KEYBYTES);
        crypto_secretstream_xchacha20poly1305_keygen(k);

        /* push */

        ret = crypto_secretstream_xchacha20poly1305_init_push(state, header, k);
        TEST_ASSERT(ret == 0);

        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c1, &res_len, m1, m1_len, NULL, 0, 0);
        TEST_ASSERT(ret == 0);
        TEST_ASSERT(res_len == m1_len + crypto_secretstream_xchacha20poly1305_ABYTES);

        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c2, NULL, m2, m2_len, ad, 0, 0);
        TEST_ASSERT(ret == 0);

        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c3, NULL, m3, m3_len, ad, ad_len,
             crypto_secretstream_xchacha20poly1305_TAG_FINAL);
        TEST_ASSERT(ret == 0);

        /* pull */

        ret = crypto_secretstream_xchacha20poly1305_init_pull(state, header, k);
        TEST_ASSERT(ret == 0);

        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m1, &res_len, &tag,
             c1, m1_len + crypto_secretstream_xchacha20poly1305_ABYTES, NULL, 0);
        TEST_ASSERT(ret == 0);
        TEST_ASSERT(tag == 0);
        TEST_ASSERT(memcmp(m1, m1_, m1_len) == 0);
        TEST_ASSERT(res_len == m1_len);

        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m2, NULL, &tag,
             c2, m2_len + crypto_secretstream_xchacha20poly1305_ABYTES, NULL, 0);
        TEST_ASSERT(ret == 0);
        TEST_ASSERT(tag == 0);
        TEST_ASSERT(memcmp(m2, m2_, m2_len) == 0);

        if (ad_len > 0) {
            ret = crypto_secretstream_xchacha20poly1305_pull
                (state, m3, NULL, &tag,
                 c3, m3_len + crypto_secretstream_xchacha20poly1305_ABYTES, NULL, 0);
            TEST_ASSERT(ret == -1);
        }
        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m3, NULL, &tag,
             c3, m3_len + crypto_secretstream_xchacha20poly1305_ABYTES, ad, ad_len);
        TEST_ASSERT(ret == 0);
        TEST_ASSERT(tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL);
        TEST_ASSERT(memcmp(m3, m3_, m3_len) == 0);

        /* previous with FINAL tag */

        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m3, NULL, &tag,
             c3, m3_len + crypto_secretstream_xchacha20poly1305_ABYTES, ad, ad_len);
        TEST_ASSERT(ret == -1);

        /* previous without a tag */

        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m2, NULL, &tag,
             c2, m2_len + crypto_secretstream_xchacha20poly1305_ABYTES, NULL, 0);
        TEST_ASSERT(ret == -1);

        /* short ciphertext */

        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m2, NULL, &tag, c2,
             randombytes_uniform(crypto_secretstream_xchacha20poly1305_ABYTES),
             NULL, 0);
        TEST_ASSERT(ret == -1);
        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m2, NULL, &tag, c2, 0, NULL, 0);
        TEST_ASSERT(ret == -1);

        /* empty ciphertext */

        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m2, NULL, &tag, c2,
             crypto_secretstream_xchacha20poly1305_ABYTES, NULL, 0);
        TEST_ASSERT(ret == -1);

        /* without explicit rekeying */

        ret = crypto_secretstream_xchacha20poly1305_init_push(state, header, k);
        TEST_ASSERT(ret == 0);
        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c1, NULL, m1, m1_len, NULL, 0, 0);
        TEST_ASSERT(ret == 0);
        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c2, NULL, m2, m2_len, NULL, 0, 0);
        TEST_ASSERT(ret == 0);

        ret = crypto_secretstream_xchacha20poly1305_init_pull(state, header, k);
        TEST_ASSERT(ret == 0);
        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m1, NULL, &tag,
             c1, m1_len + crypto_secretstream_xchacha20poly1305_ABYTES, NULL, 0);
        TEST_ASSERT(ret == 0);
        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m2, NULL, &tag,
             c2, m2_len + crypto_secretstream_xchacha20poly1305_ABYTES, NULL, 0);
        TEST_ASSERT(ret == 0);

        /* with explicit rekeying */

        ret = crypto_secretstream_xchacha20poly1305_init_push(state, header, k);
        TEST_ASSERT(ret == 0);
        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c1, NULL, m1, m1_len, NULL, 0, 0);
        TEST_ASSERT(ret == 0);

        crypto_secretstream_xchacha20poly1305_rekey(state);

        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c2, NULL, m2, m2_len, NULL, 0, 0);
        TEST_ASSERT(ret == 0);

        ret = crypto_secretstream_xchacha20poly1305_init_pull(state, header, k);
        TEST_ASSERT(ret == 0);
        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m1, NULL, &tag,
             c1, m1_len + crypto_secretstream_xchacha20poly1305_ABYTES, NULL, 0);
        TEST_ASSERT(ret == 0);

        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m2, NULL, &tag,
             c2, m2_len + crypto_secretstream_xchacha20poly1305_ABYTES, NULL, 0);
        TEST_ASSERT(ret == -1);

        crypto_secretstream_xchacha20poly1305_rekey(state);

        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m2, NULL, &tag,
             c2, m2_len + crypto_secretstream_xchacha20poly1305_ABYTES, NULL, 0);
        TEST_ASSERT(ret == 0);

        /* with explicit rekeying using TAG_REKEY */

        ret = crypto_secretstream_xchacha20poly1305_init_push(state, header, k);
        TEST_ASSERT(ret == 0);

        memcpy(statesave, state, sizeof *state);

        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c1, NULL, m1, m1_len, NULL, 0, crypto_secretstream_xchacha20poly1305_TAG_REKEY);
        TEST_ASSERT(ret == 0);

        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c2, NULL, m2, m2_len, NULL, 0, 0);
        TEST_ASSERT(ret == 0);

        memcpy(csave, c2, m2_len + crypto_secretstream_xchacha20poly1305_ABYTES);

        ret = crypto_secretstream_xchacha20poly1305_init_pull(state, header, k);
        TEST_ASSERT(ret == 0);
        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m1, NULL, &tag,
             c1, m1_len + crypto_secretstream_xchacha20poly1305_ABYTES, &tag, 0);
        TEST_ASSERT(ret == 0);
        TEST_ASSERT(tag == crypto_secretstream_xchacha20poly1305_TAG_REKEY);

        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m2, NULL, &tag,
             c2, m2_len + crypto_secretstream_xchacha20poly1305_ABYTES, &tag, 0);
        TEST_ASSERT(ret == 0);
        TEST_ASSERT(tag == 0);

        memcpy(state, statesave, sizeof *state);

        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c1, NULL, m1, m1_len, NULL, 0, 0);
        TEST_ASSERT(ret == 0);

        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c2, NULL, m2, m2_len, NULL, 0, 0);
        TEST_ASSERT(ret == 0);

        TEST_ASSERT(memcmp(csave, c2, m2_len + crypto_secretstream_xchacha20poly1305_ABYTES) != 0);

        /* New stream */

        ret = crypto_secretstream_xchacha20poly1305_init_push(state, header, k);
        TEST_ASSERT(ret == 0);

        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c1, &res_len, m1, m1_len, NULL, 0,
             crypto_secretstream_xchacha20poly1305_TAG_PUSH);
        TEST_ASSERT(ret == 0);
        TEST_ASSERT(res_len == m1_len + crypto_secretstream_xchacha20poly1305_ABYTES);

        /* Force a counter overflow, check that the key has been updated
         * even though the tag was not changed to REKEY */

        memset(state->nonce, 0xff, 4U);
        state_copy = *state;

        ret = crypto_secretstream_xchacha20poly1305_push
            (state, c2, NULL, m2, m2_len, ad, 0, 0);
        TEST_ASSERT(ret == 0);

        TEST_ASSERT(memcmp(state_copy.k, state->k, sizeof state->k) != 0);
        TEST_ASSERT(memcmp(state_copy.nonce, state->nonce, sizeof state->nonce) != 0);
        TEST_ASSERT(state->nonce[0] == 1U);
        TEST_ASSERT(sodium_is_zero(state->nonce + 1, 3U));

        ret = crypto_secretstream_xchacha20poly1305_init_pull(state, header, k);
        TEST_ASSERT(ret == 0);

        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m1, &res_len, &tag,
             c1, m1_len + crypto_secretstream_xchacha20poly1305_ABYTES, NULL, 0);
        TEST_ASSERT(ret == 0);
        TEST_ASSERT(tag == crypto_secretstream_xchacha20poly1305_TAG_PUSH);
        TEST_ASSERT(memcmp(m1, m1_, m1_len) == 0);
        TEST_ASSERT(res_len == m1_len);

        memset(state->nonce, 0xff, 4U);

        ret = crypto_secretstream_xchacha20poly1305_pull
            (state, m2, NULL, &tag,
             c2, m2_len + crypto_secretstream_xchacha20poly1305_ABYTES, NULL, 0);
        TEST_ASSERT(ret == 0);
        TEST_ASSERT(tag == 0);
        TEST_ASSERT(memcmp(m2, m2_, m2_len) == 0);

        sodium_free(m3_);
        sodium_free(m2_);
        sodium_free(m1_);
        sodium_free(m3);
        sodium_free(m2);
        sodium_free(m1);
        sodium_free(ad);
        sodium_free(csave);
        sodium_free(c3);
        sodium_free(c2);
        sodium_free(c1);
        sodium_free(k);
        sodium_free(header);
        sodium_free(statesave);
        sodium_free(state);

        TEST_ASSERT(crypto_secretstream_xchacha20poly1305_abytes() ==
               crypto_secretstream_xchacha20poly1305_ABYTES);
        TEST_ASSERT(crypto_secretstream_xchacha20poly1305_headerbytes() ==
               crypto_secretstream_xchacha20poly1305_HEADERBYTES);
        TEST_ASSERT(crypto_secretstream_xchacha20poly1305_keybytes() ==
               crypto_secretstream_xchacha20poly1305_KEYBYTES);
        TEST_ASSERT(crypto_secretstream_xchacha20poly1305_messagebytes_max() ==
               crypto_secretstream_xchacha20poly1305_MESSAGEBYTES_MAX);

        TEST_ASSERT(crypto_secretstream_xchacha20poly1305_tag_message() ==
               crypto_secretstream_xchacha20poly1305_TAG_MESSAGE);
        TEST_ASSERT(crypto_secretstream_xchacha20poly1305_tag_push() ==
               crypto_secretstream_xchacha20poly1305_TAG_PUSH);
        TEST_ASSERT(crypto_secretstream_xchacha20poly1305_tag_rekey() ==
               crypto_secretstream_xchacha20poly1305_TAG_REKEY);
        TEST_ASSERT(crypto_secretstream_xchacha20poly1305_tag_final() ==
               crypto_secretstream_xchacha20poly1305_TAG_FINAL);

        DebugPrint("OK\n");

        return 0;
    }

    void _libsodium_ut_secretstream_xchacha20poly1305()
    {
        test();
    }
} // namespace
