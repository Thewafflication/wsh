#include "wsh/core.h"

#include <stdlib.h>
#include <string.h>

static int ensure_capacity(void **ptr, size_t *capacity, size_t elem_size, size_t required)
{
    size_t new_capacity;
    void *new_ptr;

    if (required <= *capacity)
        return WSH_OK;

    new_capacity = (*capacity == 0) ? 4 : *capacity;
    while (new_capacity < required)
        new_capacity *= 2;

    new_ptr = realloc(*ptr, new_capacity * elem_size);
    if (new_ptr == NULL)
        return WSH_ERR_RESOURCE;

    *ptr = new_ptr;
    *capacity = new_capacity;
    return WSH_OK;
}

int wsh_utf8_validate(const char *text, size_t length)
{
    size_t i = 0;
    if (text == NULL)
        return WSH_ERR_INVALID;

    while (i < length) {
        unsigned char ch = (unsigned char)text[i];
        if (ch <= 0x7F) {
            ++i;
            continue;
        }
        if ((ch & 0xE0u) == 0xC0u && i + 1 < length) {
            unsigned char next = (unsigned char)text[i + 1];
            if ((next & 0xC0u) == 0x80u) {
                i += 2;
                continue;
            }
            return WSH_ERR_ENCODING;
        }
        if ((ch & 0xF0u) == 0xE0u && i + 2 < length) {
            unsigned char next1 = (unsigned char)text[i + 1];
            unsigned char next2 = (unsigned char)text[i + 2];
            if ((next1 & 0xC0u) == 0x80u && (next2 & 0xC0u) == 0x80u) {
                i += 3;
                continue;
            }
            return WSH_ERR_ENCODING;
        }
        if ((ch & 0xF8u) == 0xF0u && i + 3 < length) {
            unsigned char next1 = (unsigned char)text[i + 1];
            unsigned char next2 = (unsigned char)text[i + 2];
            unsigned char next3 = (unsigned char)text[i + 3];
            if ((next1 & 0xC0u) == 0x80u && (next2 & 0xC0u) == 0x80u && (next3 & 0xC0u) == 0x80u) {
                i += 4;
                continue;
            }
            return WSH_ERR_ENCODING;
        }
        return WSH_ERR_ENCODING;
    }

    return WSH_OK;
}

size_t wsh_utf8_codepoint_count(const char *text, size_t length)
{
    size_t count = 0;
    size_t i = 0;

    if (text == NULL)
        return 0;

    while (i < length) {
        unsigned char ch = (unsigned char)text[i];
        if (ch <= 0x7F) {
            ++count;
            ++i;
        } else if ((ch & 0xE0u) == 0xC0u) {
            ++count;
            i += 2;
        } else if ((ch & 0xF0u) == 0xE0u) {
            ++count;
            i += 3;
        } else if ((ch & 0xF8u) == 0xF0u) {
            ++count;
            i += 4;
        } else {
            ++count;
            ++i;
        }
    }

    return count;
}

wsh_bom_kind wsh_source_buffer_detect_bom(const char *buffer, size_t length)
{
    if (buffer == NULL || length < 2)
        return WSH_BOM_NONE;

    if ((unsigned char)buffer[0] == 0xFF && (unsigned char)buffer[1] == 0xFE)
        return WSH_BOM_UTF16_LE;
    if ((unsigned char)buffer[0] == 0xFE && (unsigned char)buffer[1] == 0xFF)
        return WSH_BOM_UTF16_BE;
    if (length >= 3 && (unsigned char)buffer[0] == 0xEF && (unsigned char)buffer[1] == 0xBB && (unsigned char)buffer[2] == 0xBF)
        return WSH_BOM_UTF8;

    return WSH_BOM_NONE;
}

int wsh_string_init(wsh_string *self, const char *text)
{
    size_t text_len = 0;
    if (self == NULL)
        return WSH_ERR_INVALID;
    memset(self, 0, sizeof(*self));
    if (text == NULL)
        return WSH_OK;
    while (text[text_len] != '\0')
        ++text_len;
    self->bytes = (char *)malloc(text_len + 1U);
    if (self->bytes == NULL)
        return WSH_ERR_RESOURCE;
    memcpy(self->bytes, text, text_len + 1U);
    self->length = text_len;
    self->capacity = text_len + 1U;
    return WSH_OK;
}

void wsh_string_destroy(wsh_string *self)
{
    if (self == NULL)
        return;
    free(self->bytes);
    self->bytes = NULL;
    self->length = 0;
    self->capacity = 0;
}

int wsh_string_append(wsh_string *self, const char *text)
{
    size_t text_len = 0;
    size_t new_capacity;
    char *new_bytes;

    if (self == NULL || text == NULL)
        return WSH_ERR_INVALID;

    while (text[text_len] != '\0')
        ++text_len;

    if (self->bytes == NULL) {
        self->bytes = (char *)malloc(text_len + 1U);
        if (self->bytes == NULL)
            return WSH_ERR_RESOURCE;
        self->capacity = text_len + 1U;
        self->length = 0;
        self->bytes[0] = '\0';
    }

    new_capacity = self->length + text_len + 1U;
    if (new_capacity > self->capacity) {
        new_bytes = (char *)realloc(self->bytes, new_capacity);
        if (new_bytes == NULL)
            return WSH_ERR_RESOURCE;
        self->bytes = new_bytes;
        self->capacity = new_capacity;
    }

    memcpy(self->bytes + self->length, text, text_len + 1U);
    self->length += text_len;
    return WSH_OK;
}

int wsh_list_append(wsh_list *self, const char *text)
{
    wsh_string *new_item;
    int rc;
    if (self == NULL)
        return WSH_ERR_INVALID;
    if (ensure_capacity((void **)&self->items, &self->capacity, sizeof(*self->items), self->count + 1U) != WSH_OK)
        return WSH_ERR_RESOURCE;
    new_item = (wsh_string *)calloc(1U, sizeof(*new_item));
    if (new_item == NULL)
        return WSH_ERR_RESOURCE;
    rc = wsh_string_init(new_item, text);
    if (rc != WSH_OK) {
        free(new_item);
        return rc;
    }
    self->items[self->count++] = new_item;
    return WSH_OK;
}

void wsh_list_destroy(wsh_list *self)
{
    size_t i;
    if (self == NULL)
        return;
    for (i = 0; i < self->count; ++i) {
        if (self->items[i] != NULL) {
            wsh_string_destroy(self->items[i]);
            free(self->items[i]);
        }
    }
    free(self->items);
    self->items = NULL;
    self->count = 0;
    self->capacity = 0;
}

int wsh_context_init(wsh_context *self)
{
    if (self == NULL)
        return WSH_ERR_INVALID;
    memset(self, 0, sizeof(*self));
    return WSH_OK;
}

void wsh_context_destroy(wsh_context *self)
{
    size_t i;
    if (self == NULL)
        return;
    for (i = 0; i < self->count; ++i) {
        free(self->vars[i].name);
        free(self->vars[i].value);
    }
    free(self->vars);
    memset(self, 0, sizeof(*self));
}

int wsh_context_set_variable(wsh_context *self, const char *name, const char *value)
{
    size_t i;
    char *new_name = NULL;
    char *new_value = NULL;
    wsh_variable *new_vars;

    if (self == NULL || name == NULL)
        return WSH_ERR_INVALID;

    for (i = 0; i < self->count; ++i) {
        if (strcmp(self->vars[i].name, name) == 0) {
            new_value = (char *)malloc(strlen(value == NULL ? "" : value) + 1U);
            if (new_value == NULL)
                return WSH_ERR_RESOURCE;
            strcpy(new_value, value == NULL ? "" : value);
            free(self->vars[i].value);
            self->vars[i].value = new_value;
            return WSH_OK;
        }
    }

    if (ensure_capacity((void **)&self->vars, &self->capacity, sizeof(*self->vars), self->count + 1U) != WSH_OK)
        return WSH_ERR_RESOURCE;

    new_name = (char *)malloc(strlen(name) + 1U);
    if (new_name == NULL)
        return WSH_ERR_RESOURCE;
    strcpy(new_name, name);

    new_value = (char *)malloc(strlen(value == NULL ? "" : value) + 1U);
    if (new_value == NULL) {
        free(new_name);
        return WSH_ERR_RESOURCE;
    }
    strcpy(new_value, value == NULL ? "" : value);

    new_vars = &self->vars[self->count++];
    new_vars->name = new_name;
    new_vars->value = new_value;
    new_vars->exported = 0;
    return WSH_OK;
}

int wsh_context_set_export(wsh_context *self, const char *name, int exported)
{
    size_t i;
    if (self == NULL || name == NULL)
        return WSH_ERR_INVALID;
    for (i = 0; i < self->count; ++i) {
        if (strcmp(self->vars[i].name, name) == 0) {
            self->vars[i].exported = exported;
            return WSH_OK;
        }
    }
    return WSH_ERR_INVALID;
}

const char *wsh_context_get_variable(const wsh_context *self, const char *name)
{
    size_t i;
    if (self == NULL || name == NULL)
        return NULL;
    for (i = 0; i < self->count; ++i) {
        if (strcmp(self->vars[i].name, name) == 0)
            return self->vars[i].value;
    }
    return NULL;
}

int wsh_status_list_append(wsh_status_list *self, uint32_t value)
{
    uint32_t *new_items;
    if (self == NULL)
        return WSH_ERR_INVALID;
    if (ensure_capacity((void **)&self->items, &self->capacity, sizeof(*self->items), self->count + 1U) != WSH_OK)
        return WSH_ERR_RESOURCE;
    new_items = &self->items[self->count++];
    *new_items = value;
    return WSH_OK;
}

void wsh_status_list_destroy(wsh_status_list *self)
{
    if (self == NULL)
        return;
    free(self->items);
    self->items = NULL;
    self->count = 0;
    self->capacity = 0;
}

int wsh_status_list_is_success(const wsh_status_list *self)
{
    size_t i;
    if (self == NULL)
        return 0;
    for (i = 0; i < self->count; ++i) {
        if (self->items[i] != 0)
            return 0;
    }
    return self->count > 0;
}

uint32_t wsh_status_list_last(const wsh_status_list *self)
{
    if (self == NULL || self->count == 0)
        return 0u;
    return self->items[self->count - 1U];
}
