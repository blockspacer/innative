// Copyright (c)2020 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in innative.h

#ifndef IN__OPCODES_H
#define IN__OPCODES_H

#ifdef __cplusplus
extern "C" {
#endif

enum WASM_INSTRUCTION_OPCODES
{
  OP_unreachable = 0x00,
  OP_nop         = 0x01,
  OP_block       = 0x02,
  OP_loop        = 0x03,
  OP_if          = 0x04,
  OP_else        = 0x05,
  OP_end         = 0x0b,
  OP_br          = 0x0c,
  OP_br_if       = 0x0d,
  OP_br_table    = 0x0e,
  OP_return      = 0x0f,

  // Call operators
  OP_call          = 0x10,
  OP_call_indirect = 0x11,

  // Parametric operators
  OP_drop   = 0x1a,
  OP_select = 0x1b,

  // Variable access
  OP_local_get  = 0x20,
  OP_local_set  = 0x21,
  OP_local_tee  = 0x22,
  OP_global_get = 0x23,
  OP_global_set = 0x24,

  // Memory-related operator
  OP_i32_load     = 0x28,
  OP_i64_load     = 0x29,
  OP_f32_load     = 0x2a,
  OP_f64_load     = 0x2b,
  OP_i32_load8_s  = 0x2c,
  OP_i32_load8_u  = 0x2d,
  OP_i32_load16_s = 0x2e,
  OP_i32_load16_u = 0x2f,
  OP_i64_load8_s  = 0x30,
  OP_i64_load8_u  = 0x31,
  OP_i64_load16_s = 0x32,
  OP_i64_load16_u = 0x33,
  OP_i64_load32_s = 0x34,
  OP_i64_load32_u = 0x35,
  OP_i32_store    = 0x36,
  OP_i64_store    = 0x37,
  OP_f32_store    = 0x38,
  OP_f64_store    = 0x39,
  OP_i32_store8   = 0x3a,
  OP_i32_store16  = 0x3b,
  OP_i64_store8   = 0x3c,
  OP_i64_store16  = 0x3d,
  OP_i64_store32  = 0x3e,
  OP_memory_size  = 0x3f,
  OP_memory_grow  = 0x40,

  // Constants
  OP_i32_const = 0x41,
  OP_i64_const = 0x42,
  OP_f32_const = 0x43,
  OP_f64_const = 0x44,

  // Comparison operators
  OP_i32_eqz  = 0x45,
  OP_i32_eq   = 0x46,
  OP_i32_ne   = 0x47,
  OP_i32_lt_s = 0x48,
  OP_i32_lt_u = 0x49,
  OP_i32_gt_s = 0x4a,
  OP_i32_gt_u = 0x4b,
  OP_i32_le_s = 0x4c,
  OP_i32_le_u = 0x4d,
  OP_i32_ge_s = 0x4e,
  OP_i32_ge_u = 0x4f,
  OP_i64_eqz  = 0x50,
  OP_i64_eq   = 0x51,
  OP_i64_ne   = 0x52,
  OP_i64_lt_s = 0x53,
  OP_i64_lt_u = 0x54,
  OP_i64_gt_s = 0x55,
  OP_i64_gt_u = 0x56,
  OP_i64_le_s = 0x57,
  OP_i64_le_u = 0x58,
  OP_i64_ge_s = 0x59,
  OP_i64_ge_u = 0x5a,
  OP_f32_eq   = 0x5b,
  OP_f32_ne   = 0x5c,
  OP_f32_lt   = 0x5d,
  OP_f32_gt   = 0x5e,
  OP_f32_le   = 0x5f,
  OP_f32_ge   = 0x60,
  OP_f64_eq   = 0x61,
  OP_f64_ne   = 0x62,
  OP_f64_lt   = 0x63,
  OP_f64_gt   = 0x64,
  OP_f64_le   = 0x65,
  OP_f64_ge   = 0x66,

  // Numeric operators
  OP_i32_clz      = 0x67,
  OP_i32_ctz      = 0x68,
  OP_i32_popcnt   = 0x69,
  OP_i32_add      = 0x6a,
  OP_i32_sub      = 0x6b,
  OP_i32_mul      = 0x6c,
  OP_i32_div_s    = 0x6d,
  OP_i32_div_u    = 0x6e,
  OP_i32_rem_s    = 0x6f,
  OP_i32_rem_u    = 0x70,
  OP_i32_and      = 0x71,
  OP_i32_or       = 0x72,
  OP_i32_xor      = 0x73,
  OP_i32_shl      = 0x74,
  OP_i32_shr_s    = 0x75,
  OP_i32_shr_u    = 0x76,
  OP_i32_rotl     = 0x77,
  OP_i32_rotr     = 0x78,
  OP_i64_clz      = 0x79,
  OP_i64_ctz      = 0x7a,
  OP_i64_popcnt   = 0x7b,
  OP_i64_add      = 0x7c,
  OP_i64_sub      = 0x7d,
  OP_i64_mul      = 0x7e,
  OP_i64_div_s    = 0x7f,
  OP_i64_div_u    = 0x80,
  OP_i64_rem_s    = 0x81,
  OP_i64_rem_u    = 0x82,
  OP_i64_and      = 0x83,
  OP_i64_or       = 0x84,
  OP_i64_xor      = 0x85,
  OP_i64_shl      = 0x86,
  OP_i64_shr_s    = 0x87,
  OP_i64_shr_u    = 0x88,
  OP_i64_rotl     = 0x89,
  OP_i64_rotr     = 0x8a,
  OP_f32_abs      = 0x8b,
  OP_f32_neg      = 0x8c,
  OP_f32_ceil     = 0x8d,
  OP_f32_floor    = 0x8e,
  OP_f32_trunc    = 0x8f,
  OP_f32_nearest  = 0x90,
  OP_f32_sqrt     = 0x91,
  OP_f32_add      = 0x92,
  OP_f32_sub      = 0x93,
  OP_f32_mul      = 0x94,
  OP_f32_div      = 0x95,
  OP_f32_min      = 0x96,
  OP_f32_max      = 0x97,
  OP_f32_copysign = 0x98,
  OP_f64_abs      = 0x99,
  OP_f64_neg      = 0x9a,
  OP_f64_ceil     = 0x9b,
  OP_f64_floor    = 0x9c,
  OP_f64_trunc    = 0x9d,
  OP_f64_nearest  = 0x9e,
  OP_f64_sqrt     = 0x9f,
  OP_f64_add      = 0xa0,
  OP_f64_sub      = 0xa1,
  OP_f64_mul      = 0xa2,
  OP_f64_div      = 0xa3,
  OP_f64_min      = 0xa4,
  OP_f64_max      = 0xa5,
  OP_f64_copysign = 0xa6,

  // Conversions
  OP_i32_wrap_i64      = 0xa7,
  OP_i32_trunc_f32_s   = 0xa8,
  OP_i32_trunc_f32_u   = 0xa9,
  OP_i32_trunc_f64_s   = 0xaa,
  OP_i32_trunc_f64_u   = 0xab,
  OP_i64_extend_i32_s  = 0xac,
  OP_i64_extend_i32_u  = 0xad,
  OP_i64_trunc_f32_s   = 0xae,
  OP_i64_trunc_f32_u   = 0xaf,
  OP_i64_trunc_f64_s   = 0xb0,
  OP_i64_trunc_f64_u   = 0xb1,
  OP_f32_convert_i32_s = 0xb2,
  OP_f32_convert_i32_u = 0xb3,
  OP_f32_convert_i64_s = 0xb4,
  OP_f32_convert_i64_u = 0xb5,
  OP_f32_demote_f64    = 0xb6,
  OP_f64_convert_i32_s = 0xb7,
  OP_f64_convert_i32_u = 0xb8,
  OP_f64_convert_i64_s = 0xb9,
  OP_f64_convert_i64_u = 0xba,
  OP_f64_promote_f32   = 0xbb,

  // Reinterpretations
  OP_i32_reinterpret_f32 = 0xbc,
  OP_i64_reinterpret_f64 = 0xbd,
  OP_f32_reinterpret_i32 = 0xbe,
  OP_f64_reinterpret_i64 = 0xbf,
  OP_CODE_COUNT,
};

#ifdef __cplusplus
}
#endif

#endif