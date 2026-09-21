#include "edit/utf8.h"

// Mirrors ICU U8_NEXT_OR_FFFD validation tables in Rust utf8.rs.
static const uint8_t kLeadTrail1[16] = {
    0x20, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x10, 0x30, 0x30,
};
static const uint8_t kTrail1Lead[16] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E, 0x0F, 0x0F, 0x0F, 0x00, 0x00, 0x00, 0x00,
};

void edit_utf8_chars_init(edit_utf8_chars_t *it, const uint8_t *src, size_t len, size_t offset) {
    if (it == NULL) {
        return;
    }
    it->src = src;
    it->len = len;
    it->offset = offset;
}

size_t edit_utf8_len(const edit_utf8_chars_t *it) {
    if (it == NULL) {
        return 0;
    }
    return it->len;
}

size_t edit_utf8_offset(const edit_utf8_chars_t *it) {
    if (it == NULL) {
        return 0;
    }
    return it->offset;
}

bool edit_utf8_is_empty(const edit_utf8_chars_t *it) {
    return edit_utf8_len(it) == 0;
}

bool edit_utf8_has_next(const edit_utf8_chars_t *it) {
    if (it == NULL) {
        return false;
    }
    return it->offset < it->len;
}

void edit_utf8_seek(edit_utf8_chars_t *it, size_t offset) {
    if (it == NULL) {
        return;
    }
    it->offset = offset;
}

bool edit_utf8_next(edit_utf8_chars_t *it, uint32_t *out) {
    if (it == NULL || out == NULL) {
        return false;
    }
    if (it->offset >= it->len) {
        return false;
    }
    if (it->src == NULL) {
        return false;
    }

    uint32_t cp = it->src[it->offset];
    it->offset += 1;
    if ((cp & 0x80U) == 0) {
        *out = cp;
        return true;
    }
    if (it->offset >= it->len) {
        *out = EDIT_UTF8_FFFD;
        return true;
    }

    if (cp < 0xE0U) {
        if (cp < 0xC2U) {
            *out = EDIT_UTF8_FFFD;
            return true;
        }
        cp &= 0x1FU;
    } else if (cp < 0xF0U) {
        cp &= 0x0FU;
        uint32_t t = it->src[it->offset];
        if ((kLeadTrail1[cp] & (uint8_t)(1U << (t >> 5U))) == 0) {
            *out = EDIT_UTF8_FFFD;
            return true;
        }
        cp = (cp << 6U) | (t & 0x3FU);
        it->offset += 1;
        if (it->offset >= it->len) {
            *out = EDIT_UTF8_FFFD;
            return true;
        }
    } else {
        cp &= 0x0FU;
        if (cp > 4U) {
            *out = EDIT_UTF8_FFFD;
            return true;
        }
        uint32_t t = it->src[it->offset];
        if ((kTrail1Lead[t >> 4U] & (uint8_t)(1U << cp)) == 0) {
            *out = EDIT_UTF8_FFFD;
            return true;
        }
        cp = (cp << 6U) | (t & 0x3FU);
        it->offset += 1;
        if (it->offset >= it->len) {
            *out = EDIT_UTF8_FFFD;
            return true;
        }
        t = (uint32_t)it->src[it->offset] - 0x80U;
        if (t > 0x3FU) {
            *out = EDIT_UTF8_FFFD;
            return true;
        }
        cp = (cp << 6U) | t;
        it->offset += 1;
        if (it->offset >= it->len) {
            *out = EDIT_UTF8_FFFD;
            return true;
        }
    }

    uint32_t t = (uint32_t)it->src[it->offset] - 0x80U;
    if (t > 0x3FU) {
        *out = EDIT_UTF8_FFFD;
        return true;
    }
    cp = (cp << 6U) | t;
    it->offset += 1;
    *out = cp;
    return true;
}
