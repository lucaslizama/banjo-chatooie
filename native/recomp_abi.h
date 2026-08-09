// Minimal, self-contained N64Recomp native-library ABI.
//
// The runtime dlopens this library and looks up each function named in the
// mod's `native_libraries` manifest entry as a `recomp_func_t`:
//
//     void func(uint8_t* rdram, recomp_context* ctx);
//
// Arguments arrive in a0-a3 (ctx->r4 .. ctx->r7) and the return value goes in
// v0 (ctx->r2). Pointer arguments are guest (MIPS) addresses into `rdram`, not
// host pointers -- see rdram_at() for the translation, which includes the
// byteswap the recompiler uses for sub-word accesses.
//
// This mirrors librecomp/helpers.hpp, but librecomp isn't distributed as an SDK,
// so the parts we need are reproduced here. Verified against
// N64ModernRuntime/librecomp/src/mods.cpp and N64Recomp/include/recomp.h.

#ifndef TWITCH_RECOMP_ABI_H
#define TWITCH_RECOMP_ABI_H

#include <cstdint>
#include <cstddef>
#include <string>

typedef uint64_t gpr;

typedef union {
    double d;
    struct {
        float fl;
        float fh;
    };
    struct {
        uint32_t u32l;
        uint32_t u32h;
    };
    uint64_t u64;
} fpr;

typedef struct {
    gpr r0,  r1,  r2,  r3,  r4,  r5,  r6,  r7,
        r8,  r9,  r10, r11, r12, r13, r14, r15,
        r16, r17, r18, r19, r20, r21, r22, r23,
        r24, r25, r26, r27, r28, r29, r30, r31;
    fpr f0,  f1,  f2,  f3,  f4,  f5,  f6,  f7,
        f8,  f9,  f10, f11, f12, f13, f14, f15,
        f16, f17, f18, f19, f20, f21, f22, f23,
        f24, f25, f26, f27, f28, f29, f30, f31;
    uint64_t hi, lo;
    uint32_t* f_odd;
    uint32_t status_reg;
    uint8_t mips3_float_mode;
} recomp_context;

#if defined(_WIN32)
#   define RECOMP_VISIBLE __declspec(dllexport)
#else
#   define RECOMP_VISIBLE __attribute__((visibility("default")))
#endif

#define RECOMP_EXPORT extern "C" RECOMP_VISIBLE

// `extern "C"` on a definition with an initializer warns, so exported data
// declares its linkage with a block instead.
#define RECOMP_EXPORT_DATA extern "C" { RECOMP_VISIBLE
#define RECOMP_EXPORT_DATA_END }

// KSEG0 (0x80000000) sign-extended to 64 bits, which is how the recompiler
// stores guest addresses in a gpr.
static constexpr uint64_t RDRAM_BASE = 0xFFFFFFFF80000000ull;

// Byte address of a guest address within the host rdram buffer. The `^ 3` is the
// recompiler's byteswap for byte-sized accesses; it must be applied per byte, so
// never memcpy through this pointer.
static inline uint8_t* rdram_at(uint8_t* rdram, uint32_t addr) {
    return rdram + ((((uint64_t)(int32_t)addr - RDRAM_BASE)) ^ 3);
}

static inline uint8_t mem_read_u8(uint8_t* rdram, uint32_t addr) {
    return *rdram_at(rdram, addr);
}

static inline void mem_write_u8(uint8_t* rdram, uint32_t addr, uint8_t value) {
    *rdram_at(rdram, addr) = value;
}

static inline void mem_write_u32(uint8_t* rdram, uint32_t addr, uint32_t value) {
    *(uint32_t*)(rdram + ((uint64_t)(int32_t)addr - RDRAM_BASE)) = value;
}

// Integer / pointer argument N (0-3). Guest pointers come back as uint32_t
// addresses; pass them to the mem_* helpers rather than dereferencing them.
static inline uint32_t arg_u32(recomp_context* ctx, int index) {
    return (uint32_t)(&ctx->r4)[index];
}

static inline void ret_s32(recomp_context* ctx, int32_t value) {
    ctx->r2 = (gpr)(int64_t)value;
}

// Copies a zero-terminated guest string out of rdram. `max_len` caps how far we
// are willing to walk, so a missing terminator can't run away through memory.
static inline std::string arg_string(uint8_t* rdram, recomp_context* ctx, int index, size_t max_len = 512) {
    uint32_t addr = arg_u32(ctx, index);
    std::string out;
    if (addr == 0) {
        return out;
    }
    for (size_t i = 0; i < max_len; i++) {
        char c = (char)mem_read_u8(rdram, addr + (uint32_t)i);
        if (c == '\0') {
            break;
        }
        out += c;
    }
    return out;
}

// Writes a zero-terminated string into a guest buffer, truncating to `capacity`
// (which includes the terminator).
static inline void write_string(uint8_t* rdram, uint32_t addr, const std::string& str, size_t capacity) {
    if (addr == 0 || capacity == 0) {
        return;
    }
    size_t len = str.size();
    if (len > capacity - 1) {
        len = capacity - 1;
        // Don't cut a UTF-8 sequence in half -- the UI renderer would show a
        // replacement glyph for the rest of the line. Walk back off continuation
        // bytes (10xxxxxx) until we're on a character boundary.
        while (len > 0 && ((uint8_t)str[len] & 0xC0) == 0x80) {
            len--;
        }
    }
    for (size_t i = 0; i < len; i++) {
        mem_write_u8(rdram, addr + (uint32_t)i, (uint8_t)str[i]);
    }
    mem_write_u8(rdram, addr + (uint32_t)len, 0);
}

#endif // TWITCH_RECOMP_ABI_H
