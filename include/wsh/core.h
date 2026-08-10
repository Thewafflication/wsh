#ifndef WSH_CORE_H
#define WSH_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WSH_OK 0
#define WSH_ERR_INVALID 1
#define WSH_ERR_ENCODING 2
#define WSH_ERR_RESOURCE 3
#define WSH_ERR_INTERNAL 4

typedef enum {
    WSH_BOM_NONE = 0,
    WSH_BOM_UTF8 = 1,
    WSH_BOM_UTF16_LE = 2,
    WSH_BOM_UTF16_BE = 3
} wsh_bom_kind;

typedef struct {
    char *bytes;
    size_t length;
    size_t capacity;
} wsh_string;

typedef struct {
    wsh_string **items;
    size_t count;
    size_t capacity;
} wsh_list;

typedef struct {
    char *name;
    char *value;
    int exported;
} wsh_variable;

typedef struct {
    wsh_variable *vars;
    size_t count;
    size_t capacity;
} wsh_context;

typedef struct {
    uint32_t *items;
    size_t count;
    size_t capacity;
} wsh_status_list;

int wsh_utf8_validate(const char *text, size_t length);
size_t wsh_utf8_codepoint_count(const char *text, size_t length);
wsh_bom_kind wsh_source_buffer_detect_bom(const char *buffer, size_t length);

int wsh_string_init(wsh_string *self, const char *text);
void wsh_string_destroy(wsh_string *self);
int wsh_string_append(wsh_string *self, const char *text);

int wsh_list_append(wsh_list *self, const char *text);
void wsh_list_destroy(wsh_list *self);

int wsh_context_init(wsh_context *self);
void wsh_context_destroy(wsh_context *self);
int wsh_context_set_variable(wsh_context *self, const char *name, const char *value);
int wsh_context_set_export(wsh_context *self, const char *name, int exported);
const char *wsh_context_get_variable(const wsh_context *self, const char *name);

int wsh_status_list_append(wsh_status_list *self, uint32_t value);
void wsh_status_list_destroy(wsh_status_list *self);
int wsh_status_list_is_success(const wsh_status_list *self);
uint32_t wsh_status_list_last(const wsh_status_list *self);

#ifdef __cplusplus
}
#endif

#endif
