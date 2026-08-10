#include "wsh/core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_utf8_and_bom(void)
{
    const char utf8_valid[] = "A\xC3\xA9\xF0\x9F\x98\x80";
    const char utf8_bad[] = "A\xE2\x28\xA1";
    const char utf16le_bom[] = { (char)0xFF, (char)0xFE, 'A', 0x00 };
    const char utf16be_bom[] = { (char)0xFE, (char)0xFF, 0x00, 'A' };

    assert(wsh_utf8_validate(utf8_valid, strlen(utf8_valid)) == WSH_OK);
    assert(wsh_utf8_validate(utf8_bad, strlen(utf8_bad)) == WSH_ERR_ENCODING);
    assert(wsh_utf8_codepoint_count(utf8_valid, strlen(utf8_valid)) == 4);
    assert(wsh_source_buffer_detect_bom(utf16le_bom, sizeof(utf16le_bom)) == WSH_BOM_UTF16_LE);
    assert(wsh_source_buffer_detect_bom(utf16be_bom, sizeof(utf16be_bom)) == WSH_BOM_UTF16_BE);
}

static void test_string_and_list(void)
{
    wsh_string s = {0};
    wsh_list list = {0};
    wsh_string_init(&s, "hello");
    wsh_string_append(&s, " world");
    assert(strcmp(s.bytes, "hello world") == 0);

    assert(wsh_list_append(&list, "one") == WSH_OK);
    assert(wsh_list_append(&list, "two") == WSH_OK);
    assert(list.count == 2);
    assert(strcmp(list.items[0]->bytes, "one") == 0);
    assert(strcmp(list.items[1]->bytes, "two") == 0);

    wsh_string_destroy(&s);
    wsh_list_destroy(&list);
}

static void test_context_and_status(void)
{
    wsh_context ctx = {0};
    wsh_status_list status = {0};

    assert(wsh_context_init(&ctx) == WSH_OK);
    assert(wsh_context_set_variable(&ctx, "alpha", "1") == WSH_OK);
    assert(wsh_context_set_variable(&ctx, "beta", "two") == WSH_OK);
    assert(wsh_context_set_export(&ctx, "alpha", 1) == WSH_OK);
    assert(wsh_context_get_variable(&ctx, "alpha") != NULL);

    wsh_status_list_append(&status, 0u);
    wsh_status_list_append(&status, 5u);
    assert(!wsh_status_list_is_success(&status));
    assert(wsh_status_list_last(&status) == 5u);

    wsh_context_destroy(&ctx);
    wsh_status_list_destroy(&status);
}

int main(void)
{
    test_utf8_and_bom();
    test_string_and_list();
    test_context_and_status();
    puts("portable-core-tests: PASS");
    return 0;
}
