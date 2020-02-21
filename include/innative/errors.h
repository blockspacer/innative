// Copyright (c)2020 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in innative.h

#ifndef IN__ERRORS_H
#define IN__ERRORS_H

#ifdef __cplusplus
extern "C" {
#endif

enum IN_ERROR
{
  ERR_SUCCESS = 0,

  // Parse errors that immediately terminate parsing
  ERR_PARSE_UNEXPECTED_EOF = -0x0F,
  ERR_PARSE_INVALID_MAGIC_COOKIE,
  ERR_PARSE_INVALID_VERSION,
  ERR_PARSE_INVALID_FILE_LENGTH,
  ERR_PARSE_INVALID_NAME, // Name is not a valid utf8 encoding
  ERR_UNKNOWN_COMMAND_LINE,
  ERR_MISSING_COMMAND_LINE_PARAMETER,
  ERR_NO_INPUT_FILES,
  ERR_UNKNOWN_ENVIRONMENT_ERROR,
  ERR_COMMAND_LINE_CONFLICT,
  ERR_UNKNOWN_FLAG,
  ERR_MISSING_LOADER,
  ERR_INSUFFICIENT_BUFFER,

  // Fatal errors that prevent properly parsing the module
  ERR_FATAL_INVALID_WASM_SECTION_ORDER = -0xFF,
  ERR_FATAL_INVALID_MODULE,
  ERR_FATAL_INVALID_ENCODING,
  ERR_FATAL_OVERLONG_ENCODING,
  ERR_FATAL_UNKNOWN_KIND,
  ERR_FATAL_UNKNOWN_INSTRUCTION,
  ERR_FATAL_UNKNOWN_SECTION,
  ERR_FATAL_UNKNOWN_FUNCTION_SIGNATURE,
  ERR_FATAL_UNKNOWN_TARGET,
  ERR_FATAL_EXPECTED_END_INSTRUCTION,
  ERR_FATAL_NULL_POINTER,
  ERR_FATAL_BAD_ELEMENT_TYPE,
  ERR_FATAL_BAD_HASH,
  ERR_FATAL_DUPLICATE_EXPORT,
  ERR_FATAL_DUPLICATE_MODULE_NAME,
  ERR_FATAL_FILE_ERROR,
  ERR_FATAL_LINK_ERROR,
  ERR_FATAL_TOO_MANY_LOCALS,
  ERR_FATAL_OUT_OF_MEMORY,
  ERR_FATAL_RESOURCE_ERROR,
  ERR_FATAL_NO_OUTPUT_FILE,
  ERR_FATAL_NO_MODULES,
  ERR_FATAL_NO_START_FUNCTION,
  ERR_FATAL_INVALID_INDEX,

  // Validation errors that prevent compiling the module
  ERR_VALIDATION_ERROR = -0xFFF,
  ERR_INVALID_FUNCTION_SIG,
  ERR_INVALID_FUNCTION_INDEX,
  ERR_INVALID_TABLE_INDEX,
  ERR_INVALID_MEMORY_INDEX,
  ERR_INVALID_GLOBAL_INDEX,
  ERR_INVALID_IMPORT_MEMORY_MINIMUM,
  ERR_INVALID_IMPORT_MEMORY_MAXIMUM,
  ERR_INVALID_IMPORT_TABLE_MINIMUM,
  ERR_INVALID_IMPORT_TABLE_MAXIMUM,
  ERR_INVALID_IDENTIFIER,
  ERR_INVALID_TABLE_ELEMENT_TYPE,
  ERR_INVALID_LIMITS,
  ERR_INVALID_INITIALIZER,
  ERR_INVALID_GLOBAL_INITIALIZER,
  ERR_INVALID_INITIALIZER_TYPE,
  ERR_INVALID_GLOBAL_TYPE,
  ERR_INVALID_GLOBAL_IMPORT_TYPE,
  ERR_INVALID_TABLE_TYPE,
  ERR_INVALID_MEMORY_TYPE,
  ERR_INVALID_START_FUNCTION,
  ERR_INVALID_TABLE_OFFSET,
  ERR_INVALID_MEMORY_OFFSET,
  ERR_INVALID_FUNCTION_BODY,
  ERR_INVALID_FUNCTION_IMPORT_TYPE,
  ERR_INVALID_VALUE_STACK,
  ERR_EMPTY_VALUE_STACK,
  ERR_INVALID_TYPE,
  ERR_INVALID_TYPE_INDEX,
  ERR_INVALID_BRANCH_DEPTH,
  ERR_INVALID_LOCAL_INDEX,
  ERR_INVALID_ARGUMENT_TYPE,
  ERR_INVALID_BLOCK_SIGNATURE,
  ERR_INVALID_MEMORY_ALIGNMENT,
  ERR_INVALID_RESERVED_VALUE,
  ERR_INVALID_UTF8_ENCODING,
  ERR_INVALID_DATA_SEGMENT,
  ERR_INVALID_MUTABILITY,
  ERR_INVALID_EMBEDDING,
  ERR_IMMUTABLE_GLOBAL,
  ERR_UNKNOWN_SIGNATURE_TYPE,
  ERR_UNKNOWN_MODULE,
  ERR_UNKNOWN_EXPORT,
  ERR_UNKNOWN_BLANK_IMPORT,
  ERR_EMPTY_IMPORT,
  ERR_MULTIPLE_RETURN_VALUES,
  ERR_MULTIPLE_TABLES,
  ERR_MULTIPLE_MEMORIES,
  ERR_IMPORT_EXPORT_MISMATCH,
  ERR_IMPORT_EXPORT_TYPE_MISMATCH,
  ERR_FUNCTION_BODY_MISMATCH,
  ERR_MEMORY_MINIMUM_TOO_LARGE,
  ERR_MEMORY_MAXIMUM_TOO_LARGE,
  ERR_IF_ELSE_MISMATCH,
  ERR_END_MISMATCH,
  ERR_SIGNATURE_MISMATCH,
  ERR_EXPECTED_ELSE_INSTRUCTION,
  ERR_ILLEGAL_C_IMPORT,

  // Compilation errors when parsing WAT
  ERR_WAT_INTERNAL_ERROR = -0xFFFFF,
  ERR_WAT_EXPECTED_OPEN,
  ERR_WAT_EXPECTED_CLOSE,
  ERR_WAT_EXPECTED_TOKEN,
  ERR_WAT_EXPECTED_NAME,
  ERR_WAT_EXPECTED_STRING,
  ERR_WAT_EXPECTED_VALUE,
  ERR_WAT_EXPECTED_NUMBER,
  ERR_WAT_EXPECTED_TYPE,
  ERR_WAT_EXPECTED_VAR,
  ERR_WAT_EXPECTED_VALTYPE,
  ERR_WAT_EXPECTED_FUNC,
  ERR_WAT_EXPECTED_OPERATOR,
  ERR_WAT_EXPECTED_INTEGER,
  ERR_WAT_EXPECTED_FLOAT,
  ERR_WAT_EXPECTED_RESULT,
  ERR_WAT_EXPECTED_THEN,
  ERR_WAT_EXPECTED_ELSE,
  ERR_WAT_EXPECTED_END,
  ERR_WAT_EXPECTED_LOCAL,
  ERR_WAT_EXPECTED_FUNCREF,
  ERR_WAT_EXPECTED_MUT,
  ERR_WAT_EXPECTED_MODULE,
  ERR_WAT_EXPECTED_ELEM,
  ERR_WAT_EXPECTED_KIND,
  ERR_WAT_EXPECTED_EXPORT,
  ERR_WAT_EXPECTED_IMPORT,
  ERR_WAT_EXPECTED_BINARY,
  ERR_WAT_EXPECTED_QUOTE,
  ERR_WAT_INVALID_TOKEN,
  ERR_WAT_INVALID_NUMBER,
  ERR_WAT_INVALID_IMPORT_ORDER,
  ERR_WAT_INVALID_ALIGNMENT,
  ERR_WAT_INVALID_NAME,
  ERR_WAT_INVALID_VAR,
  ERR_WAT_INVALID_TYPE,
  ERR_WAT_INVALID_LOCAL,
  ERR_WAT_UNKNOWN_TYPE,
  ERR_WAT_UNEXPECTED_NAME,
  ERR_WAT_TYPE_MISMATCH,
  ERR_WAT_LABEL_MISMATCH,
  ERR_WAT_OUT_OF_RANGE,
  ERR_WAT_BAD_ESCAPE,
  ERR_WAT_DUPLICATE_NAME,
  ERR_WAT_PARAM_AFTER_RESULT,

  // Runtime errors
  ERR_RUNTIME_INIT_ERROR = -0xFFFFFF,
  ERR_RUNTIME_TRAP,
  ERR_RUNTIME_ASSERT_FAILURE,

  // Sourcemap errors
  ERR_SOURCEMAP_ERROR = -0x1FFFFF,
  ERR_MAP_EXPECTED_OPEN_BRACE,
  ERR_MAP_EXPECTED_CLOSE_BRACE,
  ERR_MAP_EXPECTED_OPEN_BRACKET,
  ERR_MAP_EXPECTED_CLOSE_BRACKET,
  ERR_MAP_EXPECTED_QUOTE,
  ERR_MAP_EXPECTED_COMMA,
  ERR_MAP_EXPECTED_COLON,
  ERR_MAP_UNEXPECTED_END,
  ERR_MAP_UNEXPECTED_BASE64,
  ERR_MAP_INVALID_STRING,
  ERR_MAP_INVALID_NUMBER,
  ERR_MAP_UNKNOWN_KEY,
};

#ifdef __cplusplus
}
#endif

#endif