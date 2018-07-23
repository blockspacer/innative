// Copyright �2018 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in native-wasm.h

#include "wat.h"
#include "trex.h"
#include "util.h"
#include "queue.h"
#include "stack.h"
#include "parse.h"
#include "validate.h"
#include "compile.h"
#include <vector>
#include <signal.h>
#include <setjmp.h>

#define LEXER_DIGIT "0-9"
#define LEXER_HEXDIGIT "0-9A-Fa-f"
#define LEXER_LETTER "A-Za-z"
#define LEXER_NUM "([" LEXER_DIGIT "](_?[" LEXER_DIGIT "])*)"
#define LEXER_HEXNUM "([" LEXER_HEXDIGIT "](_?[" LEXER_HEXDIGIT "])*)"
#define LEXER_NAT "(" LEXER_NUM "|0x" LEXER_HEXNUM ")"
#define LEXER_FLOAT "(" LEXER_NUM "\\." LEXER_NUM "?(e|E " LEXER_NUM ")? | 0x" LEXER_HEXNUM "\\." LEXER_HEXNUM "?(p|P " LEXER_HEXNUM ")?)"
#define LEXER_NAME "\\$[" LEXER_LETTER LEXER_DIGIT "_.+\\-*/\\\\\\^~=<>!?@#$%&|:'`]+"

KHASH_INIT(tokens, StringRef, TokenID, 1, __ac_X31_hash_stringrefins, kh_int_hash_equal)

static kh_inline khint_t kh_hash_token(const Token& t)
{
  const char* s = t.pos;
  const char* end = t.pos + t.len;
  khint_t h = (khint_t)*s;

  for(++s; s < end; ++s)
    h = (h << 5) - h + (khint_t)*s;

  return h;
}

static kh_inline bool kh_equal_token(const Token& a, const Token& b)
{
  if(a.len != b.len)
    return false;
  return !memcmp(a.pos, b.pos, a.len);
}

KHASH_INIT(indexname, Token, varuint32, 1, kh_hash_token, kh_equal_token)

struct DeferAction
{
  int id;
  Token t;
  uint64_t func;
  uint64_t index;
};

struct WatState
{
  WatState(Module& mod) : m(mod)
  {
    typehash = kh_init_indexname();
    funchash = kh_init_indexname();
    tablehash = kh_init_indexname();
    memoryhash = kh_init_indexname();
    globalhash = kh_init_indexname();
  }
  ~WatState()
  {
    kh_destroy_indexname(typehash);
    kh_destroy_indexname(funchash);
    kh_destroy_indexname(tablehash);
    kh_destroy_indexname(memoryhash);
    kh_destroy_indexname(globalhash);
  }
  varuint7 GetJump(Token var)
  {
    if(var.id == TOKEN_INTEGER && var.i < std::numeric_limits<varuint7>::max())
      return (varuint7)var.i;
    if(var.id == TOKEN_NAME)
    {
      StringRef r = { var.pos, var.len };
      for(varuint7 i = 0; i < stack.Size(); ++i)
        if(stack[i] == r)
          return i;
    }

    return (varuint7)~0;
  }

  Module& m;
  Queue<DeferAction> defer;
  Stack<StringRef> stack;
  kh_indexname_t* typehash;
  kh_indexname_t* funchash;
  kh_indexname_t* tablehash;
  kh_indexname_t* memoryhash;
  kh_indexname_t* globalhash;
};

kh_tokens_t* GenTokenHash(std::initializer_list<const char*> list)
{
  kh_tokens_t* h = kh_init_tokens();

  TokenID count = 0;
  int r;
  for(const char* s : list)
  {
    auto iter = kh_put_tokens(h, StringRef{ s, strlen(s) }, &r);
    kh_val(h, iter) = ++count;
  }

  return h;
}

static kh_tokens_t* tokenhash = GenTokenHash({ "(", ")", "module", "import", "type", "start", "func", "global", "table", "memory", "export",
  "data", "elem", "offset", "align", "local", "result", "param", "i32", "i64", "f32", "f64", "anyfunc", "mut", "block", "loop",
  "if", "then", "else", "end", /* script extensions */ "binary", "quote", "register", "invoke", "get", "assert_return",
  "assert_return_canonical_nan", "assert_return_arithmetic_nan", "assert_trap", "assert_malformed", "assert_invalid",
  "assert_unlinkable", "assert_exhaustion", "script", "input", "output" });
static kh_tokens_t* assertionhash = GenTokenHash({ "alignment","out of bounds memory access", "unexpected end", "magic header not detected",
  "unknown binary version", "integer representation too long", "integer too large", "zero flag expected", "too many locals", "type mismatch",
  "mismatching label", "unknown label", "unknown function 0","constant out of range", "invalid section id", "length out of bounds",
  "function and code section have inconsistent lengths", "data segment does not fit", "unknown memory 0", "elements segment does not fit",
  "constant expression required", "duplicate export name", "unknown table", "unknown memory", "unknown operator", "unexpected token",
  "undefined element", "unknown local", "invalid mutability", "incompatible import type", "unknown import", "integer overflow"
  });
static std::string numbuf;


void TokenizeWAT(Queue<Token>& tokens, char* s, char* end)
{
  static const char* trex_err = 0;
  static TRex* regex_INT = trex_compile("^(\\+|\\-)?" LEXER_NAT, &trex_err);
  static TRex* regex_NAME = trex_compile("^" LEXER_NAME, &trex_err);
  static TRex* regex_FLOAT = trex_compile("^" "(" LEXER_NUM "\\." LEXER_NUM "?(e|E " LEXER_NUM ")?)", &trex_err);

  while(s < end)
  {
    while(s < end && (s[0] == ' ' || s[0] == '\n' || s[0] == '\r' || s[0] == '\t' || s[0] == '\f'))
      ++s;

    if(s >= end)
      break;

    switch(s[0])
    {
    case 0:
      assert(s < end);
      ++s;
      break;
    case '(':
      if(s + 1 < end && s[1] == ';') // This is a comment
      {
        s += 2;
        size_t depth = 1;
        while(depth > 0 && s < end)
        {
          switch(*s)
          {
          case '(':
            if(s + 1 < end && s[1] == ';')
              depth += 1;
            ++s;
            break;
          case ';':
            if(s + 1 < end && s[1] == ')')
              depth -= 1;
            ++s;
            break;
          }
          ++s;
        }
      }
      else
      {
        tokens.Push(Token{ TOKEN_OPEN, s });
        ++s;
      }
      break;
    case ')':
      tokens.Push(Token{ TOKEN_CLOSE, s });
      ++s;
      break;
    case ';': // A comment
    {
      if(s + 1 < end && s[1] == ';')
      {
        do
        {
          ++s;
        } while(s < end && s[0] != '\n');
      }
      else
      {
        tokens.Push(Token{ TOKEN_NONE });
        assert(false);
      }

      if(s < end)
        ++s;

      break;
    }
    case '"': // A string
    {
      const char* begin = ++s;
      while(s[0] != '"' && s + 1 < end)
        s += (s[0] == '\\' && s[1] == '"') ? 2 : 1;

      Token t = { TOKEN_STRING, begin };
      t.len = s - begin;
      tokens.Push(t);

      if(s[0] == '"')
        ++s;
      break;
    }
    case '$': // A name
    {
      const char* begin;
      const char* end;
      if(trex_search(regex_NAME, s, &begin, &end) == TRex_True)
      {
        assert(begin == s);
        ++begin; // Drop the $
        Token t = { TOKEN_NAME, begin };
        t.len = end - begin;
        tokens.Push(t);
        s = const_cast<char*>(end);
        break;
      } // If the search fails, fall through and just start trying other regexes. It will eventually be classified as an invalid token.
    }
    case '-':
    case '+':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9': // Either an integer or a float
    {
      const char* intbegin = 0;
      const char* floatbegin = 0;
      const char* end = 0;
      if(trex_search(regex_INT, s, &intbegin, &end) || trex_search(regex_FLOAT, s, &floatbegin, &end))
      {
        size_t len = end - s;
        numbuf.resize(len);
        int c = 0;
        for(int i = 0; i < len; ++i)
          if(s[i] != '_')
            numbuf[c++] = s[i];

        char* out;
        if(intbegin != nullptr)
          tokens.Push(Token{ TOKEN_INTEGER, s, (int64_t)strtoll(numbuf.data(), &out, 10) });
        else
        {
          Token t = { TOKEN_FLOAT, s };
          t.f = strtod(numbuf.data(), &out);
          tokens.Push(t);
        }

        s = const_cast<char*>(end);
        break;
      } // Fall through to attempt something else
    }
    default:
    {
      const char* begin = s;

      while(s < end && s[0] != ' ' && s[0] != '\n' && s[0] != '\r' && s[0] != '\t' && s[0] != '\f' && s[0] != '=' && s[0] != ')' && s[0] != '(' && s[0] != ';')
        ++s;

      StringRef ref = { begin, static_cast<size_t>(s - begin) };
      khiter_t iter = kh_get_tokens(tokenhash, ref);
      if(kh_exist2(tokenhash, iter))
        tokens.Push(Token{ kh_val(tokenhash, iter), begin });
      else
      {
        byte op = GetInstruction(ref);
        if(op != 0xFF)
          tokens.Push(Token{ TOKEN_OPERATOR, begin, (int64_t)op });
        else
        {
          assert(false);
          tokens.Push(Token{ TOKEN_NONE });
        }
      }
      if(*s == '=')
        ++s;
    }
    }
  }
}

#define EXPECTED(t, e, err) if((t).Size() == 0 || (t).Pop().id != (e)) return assert(false), (err)

int WatString(ByteArray& str, StringRef t)
{
  if(!t.s)
    return assert(false), ERR_PARSE_INVALID_NAME;

  if(str.bytes)
  {
    uint8_t* n = tmalloc<uint8_t>(str.n_bytes + t.len + 1);
    memcpy(n, str.bytes, str.n_bytes);
    str.bytes = n;
  }
  else
    str.bytes = tmalloc<uint8_t>(t.len + 1);

  if(!str.bytes)
    return ERR_FATAL_OUT_OF_MEMORY;

  for(int i = 0; i < t.len; ++i)
  {
    if(t.s[i] == '\\')
    {
      switch(t.s[++i])
      {
      case 'n':
        str.bytes[str.n_bytes++] = '\n';
        break;
      case 't':
        str.bytes[str.n_bytes++] = '\t';
        break;
      case '\\':
        str.bytes[str.n_bytes++] = '\\';
        break;
      case '\'':
        str.bytes[str.n_bytes++] = '\'';
        break;
      case '"':
        str.bytes[str.n_bytes++] = '"';
        break;
      case 'u':
        // TODO: evaluate unicode
      default:
        if((t.s[i] >= '0' && t.s[i] <= '9') || (t.s[i] >= 'A' && t.s[i] <= 'F'))
        {
          if((t.s[i + 1] >= '0' && t.s[i + 1] <= '9') || (t.s[i + 1] >= 'A' && t.s[i + 1] <= 'F'))
          {
            char buf[3] = { t.s[i], t.s[i + 1], 0 };
            str.bytes[str.n_bytes++] = (uint8_t)strtol(buf, 0, 16);
            ++i;
            break;
          }
        }
        return assert(false), ERR_WAT_BAD_ESCAPE;
      }
    }
    else
      str.bytes[str.n_bytes++] = t.s[i];
  }
  str.bytes[str.n_bytes] = 0;

  return ERR_SUCCESS;
}

// A string is similar to a name, but we must resolve escaped characters and hex codes
NW_FORCEINLINE int WatString(ByteArray& str, const Token& t)
{
  if(t.id != TOKEN_STRING)
    return assert(false), ERR_WAT_EXPECTED_STRING;
  return WatString(str, StringRef{ t.pos, t.len });
}

int WatName(ByteArray& name, const Token& t)
{
  if(t.id != TOKEN_NAME || !t.pos || !t.len)
    return assert(false), ERR_PARSE_INVALID_NAME;

  name.bytes = tmalloc<uint8_t>(t.len + 1);
  if(!name.bytes || t.len > std::numeric_limits<varuint32>::max())
    return assert(false), ERR_FATAL_OUT_OF_MEMORY;
  name.n_bytes = (varuint32)t.len;
  memcpy(name.bytes, t.pos, t.len);
  name.bytes[name.n_bytes] = 0;

  return ERR_SUCCESS;
}

template<class T>
int AppendArray(T item, T*& a, varuint32& n)
{
  if(!(a = (T*)realloc(a, (++n) * sizeof(T))))
    return assert(false), ERR_FATAL_OUT_OF_MEMORY;
  a[n - 1] = item;
  return ERR_SUCCESS;
}

varsint7 WatValType(TokenID id)
{
  switch(id)
  {
  case TOKEN_i32: return TE_i32;
  case TOKEN_i64: return TE_i64;
  case TOKEN_f32: return TE_f32;
  case TOKEN_f64: return TE_f64;
  }

  return 0;
}

int AddWatValType(TokenID id, varsint7*& a, varuint32& n)
{
  varsint7 ty = WatValType(id);
  if(!ty)
    return assert(false), ERR_WAT_EXPECTED_VALTYPE;
  return AppendArray<varsint7>(ty, a, n);
}

int WatTypeInner(Queue<Token>& tokens, FunctionSig& sig, const char*** names)
{
  sig.form = TE_func;
  int r;
  while(tokens.Peek().id == TOKEN_OPEN)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);

    switch(tokens.Pop().id)
    {
    case TOKEN_PARAM:
      if(tokens.Peek().id == TOKEN_NAME)
      {
        if(names) // You are legally allowed to put parameter names in typedefs in WAT, but the names are thrown away.
        {
          if(tokens.Peek().len >= std::numeric_limits<varuint32>::max())
            return assert(false), ERR_WAT_OUT_OF_RANGE;
          varuint32 len = (varuint32)tokens.Peek().len;
          char* s = tmalloc<char>(len + 1);
          memcpy(s, tokens.Peek().pos, len);
          s[len] = 0;

          len = sig.n_params;
          if(r = AppendArray<const char*>(s, *names, len))
            return r;
        }
        tokens.Pop();
        if(r = AddWatValType(tokens.Pop().id, sig.params, sig.n_params))
          return r;
      }
      else
      {
        while(tokens.Peek().id != TOKEN_CLOSE)
          if(r = AddWatValType(tokens.Pop().id, sig.params, sig.n_params))
            return r;
      }
      break;
    case TOKEN_RESULT:
      while(tokens.Peek().id != TOKEN_CLOSE)
        if(r = AddWatValType(tokens.Pop().id, sig.returns, sig.n_returns))
          return r;
      break;
    default:
      return assert(false), ERR_WAT_EXPECTED_TOKEN;
    }

    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }

  return ERR_SUCCESS;
}

int WatType(WatState& state, Queue<Token>& tokens, varuint32* index)
{
  EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
  EXPECTED(tokens, TOKEN_FUNC, ERR_WAT_EXPECTED_FUNC);

  FunctionSig sig = { 0 };
  int r = WatTypeInner(tokens, sig, 0);
  *index = state.m.type.n_functions;
  if(r = AppendArray<FunctionSig>(sig, state.m.type.functions, state.m.type.n_functions))
    return r;

  EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  return ERR_SUCCESS;
}

int WatAppendImport(Module& m, const Import& i, varuint32* index)
{
  if(m.table.n_tables > 0 || m.function.n_funcdecl > 0 || m.global.n_globals > 0 || m.memory.n_memory > 0)
    return assert(false), ERR_WAT_INVALID_IMPORT_ORDER; // If we're trying to insert an import after declaring a table/func/global/memory, fail.

  *index = m.importsection.n_import;
  if(!(m.importsection.imports = (Import*)realloc(m.importsection.imports, (++m.importsection.n_import) * sizeof(Import))))
    return assert(false), ERR_FATAL_OUT_OF_MEMORY;

  // Find the correct index to insert into
  for(varuint32 j = 0; j < m.importsection.n_import - 1; ++j)
    if(m.importsection.imports[j].kind > i.kind)
      *index = j;

  if((m.importsection.n_import - *index - 1) > 0) // Move things out of the way if we aren't at the end of the array
    memmove(m.importsection.imports + *index + 1, m.importsection.imports + *index, m.importsection.n_import - *index - 1);

  m.importsection.imports[*index] = i; // Set the value

  // Properly increment counts based on kind
  switch(i.kind)
  {
  case KIND_FUNCTION:
    ++m.importsection.functions;
  case KIND_TABLE:
    ++m.importsection.tables;
  case KIND_MEMORY:
    ++m.importsection.memory;
  case KIND_GLOBAL: // Skip incrementing the globals count, because we already did it when incrementing n_import
    break;
  }

  switch(i.kind) // Fix the index
  {
  case KIND_TABLE: *index -= m.importsection.functions; break;
  case KIND_MEMORY: *index -= m.importsection.tables; break;
  case KIND_GLOBAL: *index -= m.importsection.memory; break;
  }

  return ERR_SUCCESS;
}

varuint32 WatGetFromHash(kh_indexname_t* hash, const Token& t)
{
  if(t.id == TOKEN_INTEGER && t.i < std::numeric_limits<varuint32>::max())
    return (varuint32)t.i;
  else if(t.id == TOKEN_NAME)
  {
    khiter_t iter = kh_get_indexname(hash, t);

    if(kh_exist2(hash, iter))
      return kh_val(hash, iter);
  }

  return (varuint32)~0;
}

int WatFuncType(WatState& state, Queue<Token>& tokens, varuint32& sig, const char*** names)
{
  sig = (varuint32)~0;
  if(tokens.Size() > 1 && tokens[0].id == TOKEN_OPEN && tokens[1].id == TOKEN_TYPE)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
    EXPECTED(tokens, TOKEN_TYPE, ERR_WAT_EXPECTED_TYPE);

    if(tokens.Peek().id != TOKEN_INTEGER && tokens.Peek().id != TOKEN_NAME)
      return assert(false), ERR_WAT_EXPECTED_VAR;

    sig = WatGetFromHash(state.typehash, tokens.Pop());

    if(sig > state.m.type.n_functions)
      return assert(false), ERR_WAT_INVALID_TYPE;

    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }

  if(tokens.Size() > 1 && tokens[0].id == TOKEN_OPEN && (tokens[1].id == TOKEN_PARAM || tokens[1].id == TOKEN_RESULT))
  {
    // Create a type to match this function signature
    FunctionSig func = { 0 };
    int r = WatTypeInner(tokens, func, names);

    if(sig != (varuint32)~0) // If we already have a type, compare the two types and make sure they are identical
    {
      if(!MatchFunctionSig(state.m.type.functions[sig], func))
        return assert(false), ERR_WAT_TYPE_MISMATCH;
    }
    else
    {
      sig = state.m.type.n_functions;
      return !r ? AppendArray<FunctionSig>(func, state.m.type.functions, state.m.type.n_functions) : r;
    }
  }

  return ERR_SUCCESS;
}

// Checks if an integer is a power of two
inline bool IsPowerOfTwo(varuint32 x) noexcept
{
  return (x & (x - 1)) == 0;
}

// Given an exact power of two, quickly gets the log2 value
inline uint32_t Power2Log2(uint32_t v) noexcept
{
  assert(IsPowerOfTwo(v));
#ifdef NW_COMPILER_MSC
  unsigned long r;
  _BitScanReverse(&r, v);
#elif defined(NW_COMPILER_GCC)
  uint32_t r = (sizeof(uint32_t) << 3) - 1 - __builtin_clz(v);
#else
  const uint32_t b[] = { 0xAAAAAAAA, 0xCCCCCCCC, 0xF0F0F0F0, 0xFF00FF00, 0xFFFF0000 };
  uint32_t r = (v & b[0]) != 0;
  r |= ((v & b[4]) != 0) << 4;
  r |= ((v & b[3]) != 0) << 3;
  r |= ((v & b[2]) != 0) << 2;
  r |= ((v & b[1]) != 0) << 1;
#endif
  return r;
}

varuint32 WatGetLocal(FunctionBody& f, FunctionSig& sig, const Token& t)
{
  if(t.id == TOKEN_INTEGER && t.i < std::numeric_limits<varuint32>::max())
    return (varuint32)t.i;
  else if(t.id == TOKEN_NAME)
  {
    std::string n(t.pos, t.len);

    for(varuint32 i = 0; i < sig.n_params; ++i)
      if(!strcmp(f.param_names[i], n.c_str()))
        return i;

    for(varuint32 i = 0; i < f.n_locals; ++i)
      if(!strcmp(f.local_names[i], n.c_str()))
        return i + sig.n_params;
  }

  return (varuint32)~0;
}

int WatConstantOperator(WatState& state, Queue<Token>& tokens, Instruction& op)
{
  switch(op.opcode)
  {
  case OP_i32_const:
    if(tokens.Peek().id != TOKEN_INTEGER)
      return assert(false), ERR_WAT_EXPECTED_INTEGER;
    op.immediates[0]._varsint32 = (varsint32)tokens.Pop().i;
    break;
  case OP_i64_const:
    if(tokens.Peek().id != TOKEN_INTEGER)
      return assert(false), ERR_WAT_EXPECTED_INTEGER;
    op.immediates[0]._varsint64 = tokens.Pop().i;
    break;
  case OP_f32_const:
    if(tokens.Peek().id == TOKEN_INTEGER)
      op.immediates[0]._float32 = (float32)tokens.Pop().i;
    else if(tokens.Peek().id == TOKEN_FLOAT)
      op.immediates[0]._float32 = (float32)tokens.Pop().f;
    else
      return assert(false), ERR_WAT_EXPECTED_FLOAT;
    break;
  case OP_f64_const:
    if(tokens.Peek().id == TOKEN_INTEGER)
      op.immediates[0]._float64 = (float64)tokens.Pop().i;
    else if(tokens.Peek().id == TOKEN_FLOAT)
      op.immediates[0]._float64 = tokens.Pop().f;
    else
      return assert(false), ERR_WAT_EXPECTED_FLOAT;
    break;
  case OP_get_global: // For constant initializers, this has to be an import, and thus must always already exist by the time we reach it.
    op.immediates[0]._varuint32 = WatGetFromHash(state.globalhash, tokens.Pop());
    if(op.immediates[0]._varuint32 == (varuint32)~0)
      return assert(false), ERR_WAT_INVALID_VAR;
    break;
  default:
    return assert(false), ERR_WAT_INVALID_INITIALIZER;
  }

  return ERR_SUCCESS;
}
int WatOperator(WatState& state, Queue<Token>& tokens, FunctionBody& f, FunctionSig& sig, varuint32 index)
{
  if(tokens.Peek().id != TOKEN_OPERATOR)
    return assert(false), ERR_WAT_EXPECTED_OPERATOR;

  int r;
  if(tokens.Peek().i > 0xFF)
    return ERR_WAT_OUT_OF_RANGE;
  Instruction op = { (byte)tokens.Pop().i };

  switch(op.opcode)
  {
  case 0xFF:
    return assert(false), ERR_FATAL_UNKNOWN_INSTRUCTION;
  case OP_br:
  case OP_br_if:
    op.immediates[0]._varuint7 = state.GetJump(tokens.Pop());
    if(op.immediates[0]._varuint7 == (varuint7)~0)
      return assert(false), ERR_WAT_EXPECTED_VAR;
    break;
  case OP_get_local:
  case OP_set_local:
  case OP_tee_local:
    op.immediates[0]._varuint32 = WatGetLocal(f, sig, tokens.Pop());
    if(op.immediates[0]._varuint32 >= f.n_locals + sig.n_params)
      return assert(false), ERR_WAT_INVALID_LOCAL;
    break;
  case OP_get_global:
  case OP_set_global:
  case OP_call:
    state.defer.Push(DeferAction{ op.opcode, tokens.Pop(), index, f.n_body });
    break;
  case OP_i32_const:
  case OP_i64_const:
  case OP_f32_const:
  case OP_f64_const:
    if(r = WatConstantOperator(state, tokens, op))
      return r;
    break;
  case OP_br_table:
    do
    {
      varuint7 jump = state.GetJump(tokens.Pop());
      if(jump == (varuint7)~0)
        return assert(false), ERR_WAT_EXPECTED_VAR;

      if(r = AppendArray<varuint32>(jump, op.immediates[0].table, op.immediates[0].n_table))
        return r;
    } while(tokens.Peek().id == TOKEN_NAME || tokens.Peek().id == TOKEN_INTEGER);

    op.immediates[1]._varuint32 = op.immediates[0].table[--op.immediates[0].n_table]; // Remove last jump from table and make it the default
    break;
  case OP_call_indirect:
    if(r = WatFuncType(state, tokens, op.immediates[0]._varuint32, 0))
      return r;
    break;
  case OP_i32_load:
  case OP_i64_load:
  case OP_f32_load:
  case OP_f64_load:
  case OP_i32_store:
  case OP_i64_store:
  case OP_f32_store:
  case OP_f64_store:
  case OP_i32_load8_s:
  case OP_i32_load16_s:
  case OP_i64_load8_s:
  case OP_i64_load16_s:
  case OP_i64_load32_s:
  case OP_i32_load8_u:
  case OP_i32_load16_u:
  case OP_i64_load8_u:
  case OP_i64_load16_u:
  case OP_i64_load32_u:
  case OP_i32_store8:
  case OP_i32_store16:
  case OP_i64_store8:
  case OP_i64_store16:
  case OP_i64_store32:
    if(tokens.Peek().id == TOKEN_OFFSET)
    {
      tokens.Pop();
      if(tokens.Peek().id != TOKEN_INTEGER)
        return assert(false), ERR_WAT_EXPECTED_INTEGER;
      op.immediates[1]._varuptr = tokens.Pop().i;
    }
    if(tokens.Peek().id == TOKEN_ALIGN)
    {
      tokens.Pop();
      if(tokens.Peek().id != TOKEN_INTEGER)
        return assert(false), ERR_WAT_EXPECTED_INTEGER;
      if(tokens.Peek().i >= std::numeric_limits<memflags>::max())
        return assert(false), ERR_WAT_OUT_OF_RANGE;

      op.immediates[0]._memflags = (memflags)tokens.Pop().i;
      if(op.immediates[0]._memflags == 0 || !IsPowerOfTwo(op.immediates[0]._memflags)) // Ensure this alignment is exactly a power of two
        return ERR_WAT_INVALID_ALIGNMENT;
      op.immediates[0]._memflags = Power2Log2(op.immediates[0]._memflags); // Calculate proper power of two
    }

    break;
  }

  if(r = AppendArray<Instruction>(op, f.body, f.n_body))
    return r;
  return ERR_SUCCESS;
}

void WatLabel(WatState& state, Queue<Token>& tokens)
{
  if(tokens.Peek().id == TOKEN_NAME)
  {
    state.stack.Push(StringRef{ tokens.Peek().pos, tokens.Peek().len });
    tokens.Pop();
  }
  else
    state.stack.Push(StringRef{ 0, 0 });
}

bool CheckLabel(WatState& state, Queue<Token>& tokens)
{
  if(tokens.Peek().id == TOKEN_NAME)
  {
    Token t = tokens.Pop();
    return state.stack.Peek() == StringRef{ t.pos, t.len };
  }

  return true;
}

int WatBlockType(Queue<Token>& tokens, varsint7& out)
{
  out = TE_void;
  if(tokens.Size() > 1 && tokens[0].id == TOKEN_OPEN && tokens[1].id == TOKEN_RESULT)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
    EXPECTED(tokens, TOKEN_RESULT, ERR_WAT_EXPECTED_RESULT);

    if(tokens.Peek().id != TOKEN_CLOSE)
    {
      if(!(out = WatValType(tokens.Pop().id)))
        return assert(false), ERR_WAT_EXPECTED_VALTYPE;

      if(tokens.Peek().id != TOKEN_CLOSE)
        return assert(false), ERR_MULTIPLE_RETURN_VALUES;
    }

    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }

  if(tokens.Size() > 1 && tokens[0].id == TOKEN_OPEN && tokens[1].id == TOKEN_RESULT)
    return assert(false), ERR_MULTIPLE_RETURN_VALUES;
  return ERR_SUCCESS;
}

int WatInstruction(WatState& state, Queue<Token>& tokens, FunctionBody& f, FunctionSig& sig, varuint32 index);

int WatExpression(WatState& state, Queue<Token>& tokens, FunctionBody& f, FunctionSig& sig, varuint32 index)
{
  if(tokens.Peek().id != TOKEN_OPEN)
    return assert(false), ERR_WAT_EXPECTED_OPEN;

  int r;
  while(tokens.Peek().id == TOKEN_OPEN)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);

    varsint7 blocktype;
    switch(tokens[0].id)
    {
    case TOKEN_BLOCK:
    case TOKEN_LOOP:
    {
      Token t = tokens.Pop();
      WatLabel(state, tokens);
      if(r = WatBlockType(tokens, blocktype))
        return r;

      {
        Instruction op = { t.id == TOKEN_BLOCK ? (byte)OP_block : (byte)OP_loop };
        op.immediates[0]._varsint7 = blocktype;
        if(r = AppendArray<Instruction>(op, f.body, f.n_body))
          return r;
      }

      while(tokens.Peek().id != TOKEN_CLOSE)
        if(r = WatInstruction(state, tokens, f, sig, index))
          return r;

      if(r = AppendArray<Instruction>(Instruction{ OP_end }, f.body, f.n_body))
        return r;
      state.stack.Pop();
      break;
    }
    case TOKEN_IF:
      tokens.Pop();
      WatLabel(state, tokens);
      if(r = WatBlockType(tokens, blocktype))
        return r;

      while(tokens.Size() > 1 && tokens[0].id == TOKEN_OPEN && tokens[1].id != TOKEN_THEN)
        if(r = WatExpression(state, tokens, f, sig, index))
          return r;

      {
        Instruction op = { OP_if };
        op.immediates[0]._varsint7 = blocktype;
        if(r = AppendArray<Instruction>(op, f.body, f.n_body)) // We append the if instruction _after_ the optional condition expression
          return r;
      }

      EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN); // There must always be a Then branch
      EXPECTED(tokens, TOKEN_THEN, ERR_WAT_EXPECTED_THEN);

      while(tokens.Peek().id != TOKEN_CLOSE)
        if(r = WatInstruction(state, tokens, f, sig, index))
          return r;

      EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);

      if(tokens.Peek().id == TOKEN_OPEN) // Must be an else branch if it exists
      {
        EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
        EXPECTED(tokens, TOKEN_ELSE, ERR_WAT_EXPECTED_ELSE);

        if(r = AppendArray<Instruction>(Instruction{ OP_else }, f.body, f.n_body))
          return r;

        while(tokens.Peek().id != TOKEN_CLOSE)
          if(r = WatInstruction(state, tokens, f, sig, index))
            return r;

        EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
      }

      if(r = AppendArray<Instruction>(Instruction{ OP_end }, f.body, f.n_body))
        return r;
      state.stack.Pop();
      break;
    default:
      if(r = WatOperator(state, tokens, f, sig, index))
        return r;
      break;
    }
  }

  EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  return ERR_SUCCESS;
}

int WatInstruction(WatState& state, Queue<Token>& tokens, FunctionBody& f, FunctionSig& sig, varuint32 index)
{
  int r;
  varsint7 blocktype;
  switch(tokens[0].id)
  {
  case TOKEN_OPEN: // This must be an expression
    return WatExpression(state, tokens, f, sig, index);
  case TOKEN_BLOCK:
  case TOKEN_LOOP:
  {
    Token t = tokens.Pop();
    WatLabel(state, tokens);
    if(r = WatBlockType(tokens, blocktype))
      return r;

    {
      Instruction op = { t.id == TOKEN_BLOCK ? (byte)OP_block : (byte)OP_loop };
      op.immediates[0]._varsint7 = blocktype;
      if(r = AppendArray<Instruction>(op, f.body, f.n_body))
        return r;
    }

    while(tokens.Peek().id != TOKEN_END)
      if(r = WatInstruction(state, tokens, f, sig, index))
        return r;

    EXPECTED(tokens, TOKEN_END, ERR_WAT_EXPECTED_END);

    if(r = CheckLabel(state, tokens))
      return r;

    if(r = AppendArray<Instruction>(Instruction{ OP_end }, f.body, f.n_body))
      return r;
    state.stack.Pop();
    break;
  }
  case TOKEN_IF:
    tokens.Pop();
    WatLabel(state, tokens);
    if(r = WatBlockType(tokens, blocktype))
      return r;

    {
      Instruction op = { OP_if };
      op.immediates[0]._varsint7 = blocktype;
      if(r = AppendArray<Instruction>(op, f.body, f.n_body)) // We append the if instruction _after_ the optional condition expression
        return r;
    }

    while(tokens.Peek().id != TOKEN_ELSE && tokens.Peek().id != TOKEN_END)
      if(r = WatInstruction(state, tokens, f, sig, index))
        return r;

    if(tokens.Pop().id == TOKEN_ELSE) // Handle else branch
    {
      if(r = CheckLabel(state, tokens))
        return r;

      if(r = AppendArray<Instruction>(Instruction{ OP_else }, f.body, f.n_body))
        return r;

      while(tokens.Peek().id != TOKEN_END)
        if(r = WatInstruction(state, tokens, f, sig, index))
          return r;

      EXPECTED(tokens, TOKEN_END, ERR_WAT_EXPECTED_END);
    }

    if(r = CheckLabel(state, tokens))
      return r;

    if(r = AppendArray<Instruction>(Instruction{ OP_end }, f.body, f.n_body))
      return r;
    state.stack.Pop();
    break;
  default:
    return WatOperator(state, tokens, f, sig, index);
  }

  return ERR_SUCCESS;
}

int WatInlineImportExport(Module& m, Queue<Token>& tokens, varuint32* index, varuint7 kind, Import** out)
{
  int r;
  if(tokens.Size() > 1 && tokens[0].id == TOKEN_OPEN && tokens[1].id == TOKEN_EXPORT)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
    EXPECTED(tokens, TOKEN_EXPORT, ERR_WAT_EXPECTED_TOKEN);

    Export e = { 0 };
    e.kind = kind;
    e.index = *index; // This is fine because you can only import OR export on a declaration statement
    if(r = WatString(e.name, tokens.Pop()))
      return r;
    if(r = AppendArray<Export>(e, m.exportsection.exports, m.exportsection.n_exports))
      return r;
    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }
  else if(tokens.Size() > 1 && tokens[0].id == TOKEN_OPEN && tokens[1].id == TOKEN_IMPORT)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
    EXPECTED(tokens, TOKEN_IMPORT, ERR_WAT_EXPECTED_TOKEN);

    Import i = { 0 };
    if(r = WatString(i.module_name, tokens.Pop()))
      return r;
    if(r = WatString(i.export_name, tokens.Pop()))
      return r;
    i.kind = kind;
    if(r = WatAppendImport(m, i, index))
      return r;

    switch(i.kind) // Fix the index
    {
    case KIND_FUNCTION: *out = m.importsection.imports + *index; break;
    case KIND_TABLE: *out = m.importsection.imports + m.importsection.functions + *index; break;
    case KIND_MEMORY: *out = m.importsection.imports + m.importsection.tables + *index; break;
    case KIND_GLOBAL: *out = m.importsection.imports + m.importsection.memory + *index; break;
    }

    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }

  return ERR_SUCCESS;
}

int WatFunction(WatState& state, Queue<Token>& tokens, varuint32* index, StringRef name)
{
  int r;
  *index = state.m.importsection.functions + state.m.function.n_funcdecl;
  Import* i = 0;
  if(r = WatInlineImportExport(state.m, tokens, index, KIND_FUNCTION, &i))
    return r;

  if(i) // If this is an import, assemble the aux information and abort.
    return WatFuncType(state, tokens, i->func_desc.sig_index, &i->func_desc.param_names);

  varuint32 sig;
  FunctionBody body = { 0 };
  if(r = WatFuncType(state, tokens, sig, &body.param_names))
    return r;

  FunctionSig& desc = state.m.type.functions[sig];
  if(name.len > 0)
    if(r = WatString(body.debug_name, name))
      return r;

  // Read in all the locals
  while(tokens.Size() > 1 && tokens.Peek().id == TOKEN_OPEN && tokens[tokens.Size() - 2].id == TOKEN_LOCAL)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
    EXPECTED(tokens, TOKEN_LOCAL, ERR_WAT_EXPECTED_LOCAL);

    if(tokens.Peek().id == TOKEN_NAME)
    {
      if(tokens.Peek().len > std::numeric_limits<varuint32>::max())
        return assert(false), ERR_WAT_OUT_OF_RANGE;
      varuint32 len = (varuint32)tokens.Peek().len;
      char* s = tmalloc<char>(len + 1);
      memcpy(s, tokens.Peek().pos, len);
      s[len] = 0;

      len = body.n_locals;
      if(r = AppendArray<const char*>(s, body.local_names, len))
        return r;
      tokens.Pop();
    }

    varuint7 local = WatValType(tokens.Pop().id);
    if(!local)
      return assert(false), ERR_WAT_EXPECTED_VALTYPE;
    if(r = AppendArray<varuint7>(local, body.locals, body.n_locals))
      return r;

    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }

  // Read in all instructions
  assert(state.stack.Size() == 0);
  while(tokens.Peek().id != TOKEN_CLOSE)
  {
    if(r = WatInstruction(state, tokens, body, desc, *index))
      return r;
  }
  assert(state.stack.Size() == 0);
  if(r = AppendArray(Instruction{ OP_end }, body.body, body.n_body))
    return r;

  if(r = AppendArray(sig, state.m.function.funcdecl, state.m.function.n_funcdecl))
    return r;

  return AppendArray(body, state.m.code.funcbody, state.m.code.n_funcbody);
}

int WatResizableLimits(ResizableLimits& limits, Queue<Token>& tokens)
{
  if(tokens.Peek().id != TOKEN_INTEGER || tokens.Peek().i >= std::numeric_limits<varuint32>::max())
    return assert(false), ERR_WAT_EXPECTED_INTEGER;
  limits.minimum = (varuint32)tokens.Pop().i;

  if(tokens.Peek().id == TOKEN_INTEGER)
  {
    if(tokens.Peek().i >= std::numeric_limits<varuint32>::max())
      return assert(false), ERR_WAT_OUT_OF_RANGE;
    limits.maximum = (varuint32)tokens.Pop().i;
    limits.flags = 1;
  }

  return ERR_SUCCESS;
}

int WatTableDesc(TableDesc& t, Queue<Token>& tokens)
{
  int r;
  if(r = WatResizableLimits(t.resizable, tokens))
    return r;

  EXPECTED(tokens, TOKEN_ANYFUNC, ERR_WAT_EXPECTED_ANYFUNC);

  t.element_type = TE_anyfunc;
  return ERR_SUCCESS;
}

int WatTable(WatState& state, Queue<Token>& tokens, varuint32* index)
{
  int r;
  *index = state.m.table.n_tables;
  Import* i = 0;
  if(r = WatInlineImportExport(state.m, tokens, index, KIND_TABLE, &i))
    return r;

  if(i) // If this is an import, assemble the aux information and abort.
    return WatTableDesc(i->table_desc, tokens);

  TableDesc table = { 0 };
  switch(tokens.Peek().id)
  {
  case TOKEN_INTEGER:
    if(r = WatTableDesc(table, tokens))
      return r;
    break;
  default:
    EXPECTED(tokens, TOKEN_ANYFUNC, ERR_WAT_EXPECTED_ANYFUNC);

    table.element_type = TE_anyfunc;

    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
    EXPECTED(tokens, TOKEN_ELEM, ERR_WAT_EXPECTED_ELEM);

    {
      TableInit init;
      init.index = *index;
      init.offset = Instruction{ OP_i32_const, 0 };

      while(tokens.Peek().id != TOKEN_CLOSE)
      {
        varuint32 f = WatGetFromHash(state.funchash, tokens.Pop());
        if(f == (varuint32)~0)
          return assert(false), ERR_WAT_INVALID_VAR;
        if(r = AppendArray(f, init.elems, init.n_elems))
          return r;
      }

      table.resizable.minimum = init.n_elems;
      table.resizable.flags = 0;
    }

    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }

  return AppendArray(table, state.m.table.tables, state.m.table.n_tables);
}

int WatInitializer(WatState& state, Queue<Token>& tokens, Instruction& op)
{
  if(tokens.Peek().id != TOKEN_OPERATOR)
    return assert(false), ERR_WAT_EXPECTED_OPERATOR;

  int r;
  if(tokens.Peek().i > 0xFF)
    return ERR_WAT_OUT_OF_RANGE;
  op.opcode = (byte)tokens.Pop().i;

  if(r = WatConstantOperator(state, tokens, op))
    return r;

  if(tokens.Peek().id != TOKEN_CLOSE)
    return assert(false), ERR_WAT_INVALID_INITIALIZER;

  return ERR_SUCCESS;
}

int WatGlobalDesc(GlobalDesc& g, Queue<Token>& tokens)
{
  if(tokens.Peek().id == TOKEN_OPEN)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
    EXPECTED(tokens, TOKEN_MUT, ERR_WAT_EXPECTED_MUT);
    g.mutability = true;
    if(!(g.type = WatValType(tokens.Pop().id)))
      return assert(false), ERR_WAT_EXPECTED_VALTYPE;

    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }
  else
  {
    g.mutability = false;
    if(!(g.type = WatValType(tokens.Pop().id)))
      return assert(false), ERR_WAT_EXPECTED_VALTYPE;
  }

  return ERR_SUCCESS;
}

int WatGlobal(WatState& state, Queue<Token>& tokens, varuint32* index)
{
  int r;
  *index = state.m.global.n_globals;
  Import* i = 0;
  if(r = WatInlineImportExport(state.m, tokens, index, KIND_GLOBAL, &i))
    return r;

  if(i) // If this is an import, assemble the aux information and abort.
    return WatGlobalDesc(i->global_desc, tokens);

  GlobalDecl g = { 0 };
  if(r = WatGlobalDesc(g.desc, tokens))
    return r;

  if(r = WatInitializer(state, tokens, g.init))
    return r;

  return AppendArray(g, state.m.global.globals, state.m.global.n_globals);
}

int WatMemoryDesc(MemoryDesc& m, Queue<Token>& tokens)
{
  return WatResizableLimits(m.limits, tokens);
}

int WatMemory(WatState& state, Queue<Token>& tokens, varuint32* index)
{
  int r;
  *index = state.m.memory.n_memory;
  Import* i = 0;
  if(r = WatInlineImportExport(state.m, tokens, index, KIND_MEMORY, &i))
    return r;

  if(i) // If this is an import, assemble the aux information and abort.
    return WatMemoryDesc(i->mem_desc, tokens);

  MemoryDesc mem = { 0 };

  if(tokens.Size() > 1 && tokens[0].id == TOKEN_OPEN && tokens[1].id == TOKEN_DATA)
  {
    DataInit init = { 0 };
    init.index = *index;
    init.offset = Instruction{ OP_i32_const, 0 };

    while(tokens[0].id != TOKEN_CLOSE)
    {
      if(tokens[0].id != TOKEN_STRING)
        return assert(false), ERR_WAT_EXPECTED_STRING;
      if(r = WatString(init.data, tokens.Pop()))
        return r;
    }

    mem.limits.flags = 0;
    mem.limits.minimum = init.data.n_bytes;
  }
  else if(r = WatMemoryDesc(mem, tokens))
    return r;

  return AppendArray(mem, state.m.memory.memory, state.m.memory.n_memory);
}

Token GetWatNameToken(Queue<Token>& tokens)
{
  return (tokens.Peek().id == TOKEN_NAME) ? tokens.Pop() : Token{ TOKEN_NONE };
}

int AddWatName(kh_indexname_t* h, Token t, varuint32 index)
{
  if(t.id == TOKEN_NAME)
  {
    int r;
    khiter_t iter = kh_put_indexname(h, t, &r);
    if(!r)
      return assert(false), ERR_WAT_DUPLICATE_NAME;
    if(iter != kh_end(h))
      kh_val(h, iter) = index;
  }

  return ERR_SUCCESS;
}

int WatImport(WatState& state, Queue<Token>& tokens)
{
  Import i = { 0 };
  int r;
  if(r = WatString(i.module_name, tokens.Pop()))
    return r;
  if(r = WatString(i.export_name, tokens.Pop()))
    return r;

  EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);

  Token t = tokens.Pop();
  Token name = GetWatNameToken(tokens);
  kh_indexname_t* hash = 0;
  switch(t.id)
  {
  case TOKEN_FUNC:
    if(r = WatName(i.func_desc.debug_name, name))
      return r;
    if(r = WatFuncType(state, tokens, i.func_desc.sig_index, &i.func_desc.param_names))
      return r;
    hash = state.funchash;
    break;
  case TOKEN_GLOBAL:
    if(r = WatGlobalDesc(i.global_desc, tokens))
      return r;
    hash = state.globalhash;
    break;
  case TOKEN_TABLE:
    if(r = WatTableDesc(i.table_desc, tokens))
      return r;
    hash = state.tablehash;
    break;
  case TOKEN_MEMORY:
    if(r = WatMemoryDesc(i.mem_desc, tokens))
      return r;
    hash = state.memoryhash;
    break;
  default:
    return assert(false), ERR_WAT_EXPECTED_KIND;
  }
  EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);

  varuint32 index;
  if(r = WatAppendImport(state.m, i, &index))
    return r;

  return AddWatName(hash, name, index);
}

template<int(*F)(WatState&, Queue<Token>&, varuint32*)>
int WatIndexProcess(WatState& state, Queue<Token>& tokens, kh_indexname_t* hash)
{
  Token t = GetWatNameToken(tokens);

  int r;
  varuint32 index = (varuint32)~0;
  if(r = (*F)(state, tokens, &index))
    return r;
  assert(index != (varuint32)~0);

  return AddWatName(hash, t, index);
}

int WatExport(WatState& state, Queue<Token>& tokens)
{
  Export e = { 0 };
  int r;
  if(r = WatString(e.name, tokens.Pop()))
    return r;

  switch(tokens.Pop().id)
  {
  case TOKEN_FUNC:
    e.kind = KIND_FUNCTION;
    e.index = WatGetFromHash(state.funchash, tokens.Pop());
    break;
  case TOKEN_GLOBAL:
    e.kind = KIND_GLOBAL;
    e.index = WatGetFromHash(state.globalhash, tokens.Pop());
    break;
  case TOKEN_TABLE:
    e.kind = KIND_TABLE;
    e.index = WatGetFromHash(state.tablehash, tokens.Pop());
    break;
  case TOKEN_MEMORY:
    e.kind = KIND_MEMORY;
    e.index = WatGetFromHash(state.memoryhash, tokens.Pop());
    break;
  default:
    return assert(false), ERR_WAT_EXPECTED_KIND;
  }

  return AppendArray(e, state.m.exportsection.exports, state.m.exportsection.n_exports);
}

int WatElemData(WatState& state, Queue<Token>& tokens, varuint32& index, Instruction& op, kh_indexname_t* hash)
{
  if(tokens[0].id == TOKEN_INTEGER || tokens[0].id == TOKEN_NAME)
    index = WatGetFromHash(hash, tokens.Pop());

  if(index == ~0)
    return assert(false), ERR_WAT_INVALID_VAR;

  if(tokens[0].id == TOKEN_OPEN)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
    if(tokens[0].id == TOKEN_OFFSET)
      tokens.Pop();

    int r;
    if(tokens.Peek().i > 0xFF)
      return ERR_WAT_OUT_OF_RANGE;
    op = { (byte)tokens.Pop().i };
    if(r = WatConstantOperator(state, tokens, op))
      return r;

    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }

  return ERR_SUCCESS;
}

int WatElem(WatState& state, Queue<Token>& tokens)
{
  TableInit e = { 0 };
  int r;
  if(r = WatElemData(state, tokens, e.index, e.offset, state.tablehash))
    return r;

  while(tokens[0].id != TOKEN_CLOSE)
  {
    AppendArray(WatGetFromHash(state.funchash, tokens.Pop()), e.elems, e.n_elems);
    if(e.elems[e.n_elems - 1] == (varuint32)~0)
      return assert(false), ERR_WAT_INVALID_VAR;
  }

  return AppendArray(e, state.m.element.elements, state.m.element.n_elements);
}

int WatData(WatState& state, Queue<Token>& tokens)
{
  DataInit d = { 0 };
  int r;
  if(r = WatElemData(state, tokens, d.index, d.offset, state.memoryhash))
    return r;

  while(tokens[0].id != TOKEN_CLOSE)
  {
    if(tokens[0].id != TOKEN_STRING)
      return assert(false), ERR_WAT_EXPECTED_STRING;
    WatString(d.data, tokens.Pop());
  }

  return AppendArray(d, state.m.data.data, state.m.data.n_data);
}

// Skips over an entire section of tokens by counting paranthesis, assuming they are well-formed
void SkipSection(Queue<Token>& tokens)
{
  int count = 1; // Assume we are already inside a section
  while(tokens.Size())
  {
    if(tokens[0].id == TOKEN_OPEN)
      ++count;
    else if(tokens[0].id == TOKEN_CLOSE)
    {
      if(!--count)
        break; // Deliberately do not pop the CLOSE token because we usually need it afterwards
    }
    tokens.Pop();
  }
}

int WatModule(Environment& env, Module& m, Queue<Token>& tokens, StringRef name)
{
  int r;
  memset(&m, 0, sizeof(Module));
  if(name.s)
    if(r = WatName(m.name, Token{ TOKEN_NAME, (const char*)name.s, (int64_t)name.len }))
      return r;

  if(tokens.Peek().id == TOKEN_NAME)
    if(r = WatName(m.name, tokens.Pop()))
      return r;

  WatState state(m);

  Token t;
  size_t restore = tokens.GetPosition();
  while(tokens.Size() > 0 && tokens.Peek().id != TOKEN_CLOSE)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
    t = tokens.Pop();
    switch(t.id) // This initial pass is for types only
    {
    case TOKEN_TYPE:
      if(r = WatIndexProcess<WatType>(state, tokens, state.typehash))
        return r;
      break;
    default:
      SkipSection(tokens);
      break;
    }
    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }

  // This is the main pass for functions/imports/etc. and also identifies illegal tokens
  tokens.SetPosition(restore);
  while(tokens.Size() > 0 && tokens.Peek().id != TOKEN_CLOSE)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
    t = tokens.Pop();
    switch(t.id)
    {
    case TOKEN_FUNC:
    {
      khiter_t iter = kh_end(state.funchash);
      if(tokens.Peek().id == TOKEN_NAME)
      {
        iter = kh_put_indexname(state.funchash, tokens.Pop(), &r);
        if(!r)
          return assert(false), ERR_WAT_DUPLICATE_NAME;
      }

      StringRef ref = { 0,0 };
      if(iter != kh_end(state.funchash))
        ref = { kh_key(state.funchash, iter).pos, kh_key(state.funchash, iter).len };
      varuint32 index;
      if(r = WatFunction(state, tokens, &index, ref))
        return r;

      if(iter != kh_end(state.funchash))
        kh_val(state.funchash, iter) = index;
      break;
    }
    case TOKEN_IMPORT:
      if(r = WatImport(state, tokens))
        return r;
      break;
    case TOKEN_TABLE:
      if(r = WatIndexProcess<WatTable>(state, tokens, state.tablehash))
        return r;
      break;
    case TOKEN_MEMORY:
      if(r = WatIndexProcess<WatMemory>(state, tokens, state.memoryhash))
        return r;
      break;
    case TOKEN_GLOBAL:
      if(r = WatIndexProcess<WatGlobal>(state, tokens, state.globalhash))
        return r;
      break;
    case TOKEN_EXPORT:
    case TOKEN_TYPE:
    case TOKEN_ELEM:
    case TOKEN_DATA:
    case TOKEN_START:
      SkipSection(tokens);
      break;
    default:
      return assert(false), ERR_WAT_INVALID_TOKEN;
    }
    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }

  // This pass resolves exports, elem, data, and the start function, to minimize deferred actions
  tokens.SetPosition(restore);
  while(tokens.Size() > 0 && tokens.Peek().id != TOKEN_CLOSE)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
    t = tokens.Pop();
    switch(t.id)
    {
    case TOKEN_EXPORT:
      if(r = WatExport(state, tokens))
        return r;
      break;
    case TOKEN_ELEM:
      if(r = WatElem(state, tokens))
        return r;
      break;
    case TOKEN_DATA:
      if(r = WatData(state, tokens))
        return r;
      break;
    case TOKEN_START:
      if(tokens[0].id != TOKEN_INTEGER && tokens[0].id != TOKEN_NAME)
        return assert(false), ERR_WAT_EXPECTED_VAR;
      m.start = WatGetFromHash(state.funchash, tokens.Pop());
      m.knownsections |= (1 << SECTION_START);
      if(m.start == (varuint32)~0)
        return assert(false), ERR_WAT_INVALID_VAR;
      break;
    default:
      SkipSection(tokens);
      break;
    }
    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }

  // Process all deferred actions
  while(state.defer.Size() > 0)
  {
    if(state.defer[0].func < m.importsection.functions || state.defer[0].func >= m.code.n_funcbody + m.importsection.functions)
      return assert(false), ERR_INVALID_FUNCTION_INDEX;
    varuint32 e;
    switch(state.defer[0].id)
    {
    case OP_get_global:
    case OP_set_global:
      e = WatGetFromHash(state.globalhash, state.defer[0].t);
      break;
    case OP_call:
      e = WatGetFromHash(state.funchash, state.defer[0].t);
      break;
    default:
      return assert(false), ERR_WAT_INVALID_TOKEN;
    }
    auto& f = m.code.funcbody[state.defer[0].func - m.importsection.functions];
    if(state.defer[0].index >= f.n_body)
      return ERR_INVALID_FUNCTION_BODY;
    f.body[state.defer[0].index].immediates[0]._varuint32 = e;
    state.defer.Pop();
  }

  m.exports = kh_init_exports();
  return ParseExportFixup(m);
}
int WatEnvironment(Environment& env, Queue<Token>& tokens)
{
  return 0;
}

int ParseWatModule(Environment& env, Module& m, uint8_t* data, size_t sz, StringRef name)
{
  Queue<Token> tokens;
  TokenizeWAT(tokens, (char*)data, (char*)data + sz);

  EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
  EXPECTED(tokens, TOKEN_MODULE, ERR_WAT_EXPECTED_MODULE);
  int r = WatModule(env, m, tokens, name);
  if(!r)
    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  return r;
}

int ParseWatScriptModule(Environment& env, Queue<Token>& tokens, kh_modules_t* mapping, Module*& last, void*& cache)
{
  EXPECTED(tokens, TOKEN_MODULE, ERR_WAT_EXPECTED_MODULE);
  int r;

  cache = 0;
  env.modules = (Module*)realloc(env.modules, ++env.n_modules * sizeof(Module));
  last = &env.modules[env.n_modules - 1];
  if(tokens[0].id == TOKEN_BINARY || (tokens.Size() > 1 && tokens[1].id == TOKEN_BINARY))
  {
    Token name = GetWatNameToken(tokens);
    tokens.Pop();
    ByteArray binary;
    if(r = WatString(binary, tokens.Pop()))
      return r;
    Stream s = { (uint8_t*)binary.bytes, binary.n_bytes, 0 };
    if(r = ParseModule(s, *last, ByteArray{ (varuint32)name.len, (uint8_t*)name.pos }))
      return r;
    if(name.id == TOKEN_NAME) // Override name if it exists
      if(r = WatName(last->name, name))
        return r;
  }
  else if(tokens[0].id == TOKEN_QUOTE || (tokens.Size() > 1 && tokens[1].id == TOKEN_QUOTE))
  {
    Token name = GetWatNameToken(tokens);
    tokens.Pop();
    Token t = tokens.Pop();
    if(r = ParseWatModule(env, *last, (uint8_t*)t.pos, t.len, StringRef{ name.pos, name.len }))
      return r;
    if(name.id == TOKEN_NAME) // Override name if it exists
      if(r = WatName(last->name, name))
        return r;
  }
  else if(r = WatModule(env, *last, tokens, StringRef{ 0,0 }))
    return r;

  if(last->name.n_bytes > 0)
  {
    khiter_t iter = kh_put_modules(mapping, (const char*)last->name.bytes, &r);
    if(!r)
      return ERR_FATAL_DUPLICATE_MODULE_NAME;
    kh_val(mapping, iter) = (varuint32)env.n_modules - 1;
  }

  return ERR_SUCCESS;
}

varuint32 GetMapping(kh_modules_t* mapping, const Token& t)
{
  khiter_t iter = kh_get_modules(mapping, std::string(t.pos, t.len).c_str());
  return kh_exist2(mapping, iter) ? kh_val(mapping, iter) : (varuint32)~0;
}

jmp_buf jump_location;

void CrashHandler(int)
{
  longjmp(jump_location, 1);
}

int CompileScript(Environment& env, const char* out, void*& cache)
{
  int r;
  ValidateEnvironment(env);
  if(env.errors)
    return ERR_VALIDATION_ERROR;
  if(r = CompileEnvironment(&env, out))
    return r;

  // Prepare to handle exceptions from the initialization
  signal(SIGILL, CrashHandler);
  int jmp = setjmp(jump_location);
  if(jmp)
    return ERR_RUNTIME_INIT_ERROR;
  cache = LoadDLL(out);
  signal(SIGILL, SIG_DFL);
  return !cache ? ERR_RUNTIME_INIT_ERROR : ERR_SUCCESS;
}

struct WatResult
{
  TYPE_ENCODING type;
  union
  {
    int32_t i32;
    int64_t i64;
    float f32;
    double f64;
  };
};

template<TYPE_ENCODING R>
bool MatchFuncSig(FunctionSig sig)
{
  if(sig.n_params > 0)
    return false;
  if(R == TE_void && sig.n_returns > 0)
    return false;
  if(R != TE_void && (!sig.n_returns || sig.returns[0] != (varsint7)R))
    return false;
  return true;
}

template<TYPE_ENCODING R, TYPE_ENCODING P, TYPE_ENCODING... Args>
bool MatchFuncSig(FunctionSig sig)
{
  if(!sig.n_params)
    return false;
  if(sig.params[0] != (varsint7)P)
    return false;
  sig.params++;
  sig.n_params--;
  return MatchFuncSig<R, Args...>(sig);
}

int ParseWatScriptAction(Environment& env, Queue<Token>& tokens, kh_modules_t* mapping, Module*& last, void*& cache, WatResult& result)
{
  int r;
  if(!cache) // If cache is null we need to recompile the current environment
  {
    if(r = CompileScript(env, "wast.dll", cache))
      return r;
    assert(cache);
  }

  switch(tokens.Pop().id)
  {
  case TOKEN_INVOKE:
  {
    Token name = GetWatNameToken(tokens);
    Module* m = last;
    if(name.id == TOKEN_NAME)
    {
      varuint32 i = GetMapping(mapping, name);
      if(i >= env.n_modules)
        return ERR_PARSE_INVALID_NAME;
      m = env.modules + i;
    }
    if(!m)
      return ERR_FATAL_INVALID_MODULE;

    ByteArray func;
    if(r = WatString(func, tokens.Pop()))
      return r;

    khiter_t iter = kh_get_exports(m->exports, (const char*)func.bytes);
    if(!kh_exist2(m->exports, iter))
      return ERR_INVALID_FUNCTION_INDEX;
    Export& e = m->exportsection.exports[kh_val(m->exports, iter)];
    if(e.kind != KIND_FUNCTION || e.index >= m->function.n_funcdecl || m->function.funcdecl[e.index] >= m->type.n_functions)
      return ERR_INVALID_FUNCTION_INDEX;

    void* f = LoadDLLFunction(cache, MergeName((const char*)m->name.bytes, (const char*)func.bytes).c_str());
    if(!f)
      return ERR_INVALID_FUNCTION_INDEX;
    
    // Dig up the exported function signature from the module and assemble a C function pointer from it
    FunctionSig& sig = m->type.functions[m->function.funcdecl[e.index]];

    if(!sig.n_returns)
      result.type = TE_void;
    else
      result.type = (TYPE_ENCODING)sig.returns[0];

    // Call the function and set the correct result.
    signal(SIGILL, CrashHandler);
    int jmp = setjmp(jump_location);
    if(jmp)
      return ERR_RUNTIME_TRAP;

    std::vector<Instruction> params;
    while(tokens.Peek().id == TOKEN_OPEN)
    {
      WatState st(*m);
      params.emplace_back();
      EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
      if(r = WatInitializer(st, tokens, params.back()))
        return r;
      EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
    }

    if(params.size() != sig.n_params)
      return ERR_SIGNATURE_MISMATCH;

    if(MatchFuncSig<TE_i32, TE_i32>(sig))
      result.i32 = static_cast<int32_t (*)(int32_t)>(f)(params[0].immediates[0]._varsint32);
    else if(MatchFuncSig<TE_i64, TE_i32>(sig))
      result.i64 = static_cast<int64_t(*)(int32_t)>(f)(params[0].immediates[0]._varsint32);
    else if(MatchFuncSig<TE_f32, TE_i32>(sig))
      result.f32 = static_cast<float(*)(int32_t)>(f)(params[0].immediates[0]._varsint32);
    else if(MatchFuncSig<TE_f64, TE_i32>(sig))
      result.f64 = static_cast<double(*)(int32_t)>(f)(params[0].immediates[0]._varsint32);
    else if(MatchFuncSig<TE_i32, TE_i64>(sig))
      result.i32 = static_cast<int32_t(*)(int64_t)>(f)(params[0].immediates[0]._varsint64);
    else if(MatchFuncSig<TE_i64, TE_i64>(sig))
      result.i64 = static_cast<int64_t(*)(int64_t)>(f)(params[0].immediates[0]._varsint64);
    else if(MatchFuncSig<TE_f32, TE_i64>(sig))
      result.f32 = static_cast<float(*)(int64_t)>(f)(params[0].immediates[0]._varsint64);
    else if(MatchFuncSig<TE_f64, TE_i64>(sig))
      result.f64 = static_cast<double(*)(int64_t)>(f)(params[0].immediates[0]._varsint64);
    else if(MatchFuncSig<TE_i32, TE_f32>(sig))
      result.i32 = static_cast<int32_t(*)(float)>(f)(params[0].immediates[0]._float32);
    else if(MatchFuncSig<TE_i64, TE_f32>(sig))
      result.i64 = static_cast<int64_t(*)(float)>(f)(params[0].immediates[0]._float32);
    else if(MatchFuncSig<TE_f32, TE_f32>(sig))
      result.f32 = static_cast<float(*)(float)>(f)(params[0].immediates[0]._float32);
    else if(MatchFuncSig<TE_f64, TE_f32>(sig))
      result.f64 = static_cast<double(*)(float)>(f)(params[0].immediates[0]._float32);
    else if(MatchFuncSig<TE_i32, TE_f64>(sig))
      result.i32 = static_cast<int32_t(*)(double)>(f)(params[0].immediates[0]._float64);
    else if(MatchFuncSig<TE_i64, TE_f64>(sig))
      result.i64 = static_cast<int64_t(*)(double)>(f)(params[0].immediates[0]._float64);
    else if(MatchFuncSig<TE_f32, TE_f64>(sig))
      result.f32 = static_cast<float(*)(double)>(f)(params[0].immediates[0]._float64);
    else if(MatchFuncSig<TE_f64, TE_f64>(sig))
      result.f64 = static_cast<double(*)(double)>(f)(params[0].immediates[0]._float64);
    else if(MatchFuncSig<TE_i32, TE_i32, TE_i32>(sig))
      result.i32 = static_cast<int32_t(*)(int32_t, int32_t)>(f)(params[0].immediates[0]._varsint32, params[0].immediates[0]._varsint32);
    else if(MatchFuncSig<TE_i64, TE_i32, TE_i32>(sig))
      result.i64 = static_cast<int64_t(*)(int32_t, int32_t)>(f)(params[0].immediates[0]._varsint32, params[0].immediates[0]._varsint32);
    else if(MatchFuncSig<TE_f32, TE_i32, TE_i32>(sig))
      result.f32 = static_cast<float(*)(int32_t, int32_t)>(f)(params[0].immediates[0]._varsint32, params[0].immediates[0]._varsint32);
    else if(MatchFuncSig<TE_f64, TE_i32, TE_i32>(sig))
      result.f64 = static_cast<double(*)(int32_t, int32_t)>(f)(params[0].immediates[0]._varsint32, params[0].immediates[0]._varsint32);
    else if(MatchFuncSig<TE_f32, TE_f32, TE_f32>(sig))
      result.f32 = static_cast<float(*)(float, float)>(f)(params[0].immediates[0]._float32, params[0].immediates[0]._float32);
    else if(MatchFuncSig<TE_f64, TE_f64, TE_f64>(sig))
      result.f64 = static_cast<double(*)(double, double)>(f)(params[0].immediates[0]._float64, params[0].immediates[0]._float64);
    else if(MatchFuncSig<TE_i64, TE_i64, TE_i64>(sig))
      result.i64 = static_cast<int64_t(*)(int64_t, int64_t)>(f)(params[0].immediates[0]._varsint64, params[0].immediates[0]._varsint64);
    else
      assert(false);

    signal(SIGILL, SIG_DFL);
    break;
  }
  case TOKEN_GET:
    assert(false); // TODO: We have no way of getting globals out of DLLs yet
    break;
  default:
    return ERR_WAT_EXPECTED_TOKEN;
  }

  return ERR_SUCCESS;
}

bool WatIsNaN(float f, bool canonical)
{
  if(!isnan(f))
    return false;
  return ((*reinterpret_cast<uint32_t*>(&f) & 0b00000000010000000000000000000000U) != 0) != canonical;
}

bool WatIsNaN(double f, bool canonical)
{
  if(!isnan(f))
    return false;
  return ((*reinterpret_cast<uint64_t*>(&f) & 0b0000000000001000000000000000000000000000000000000000000000000000ULL) != 0) != canonical;
}

// This parses an entire extended WAT testing script into an environment
int ParseWat(Environment& env, uint8_t* data, size_t sz)
{
  Queue<Token> tokens;
  TokenizeWAT(tokens, (char*)data, (char*)data + sz);

  int r;
  kh_modules_t* mapping = kh_init_modules(); // This is a special mapping for all modules using the module name itself, not just registered ones.
  Module* last = 0; // For anything not providing a module name, this was the most recently defined module.
  void* cache = 0;

  while(tokens.Size() > 0 && tokens[0].id != TOKEN_CLOSE)
  {
    EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
    switch(tokens[0].id)
    {
    case TOKEN_MODULE:
      if(r = ParseWatScriptModule(env, tokens, mapping, last, cache))
        return r;
      break;
    case TOKEN_REGISTER:
    {
      tokens.Pop();
      varuint32 i = ~0;
      if(last)
        i = last - env.modules;
      if(tokens[0].id == TOKEN_NAME)
        i = GetMapping(mapping, tokens.Pop());
      if(i == ~0)
        return ERR_PARSE_INVALID_NAME;
        
      ByteArray name;
      if(r = WatString(name, tokens.Pop()))
        return r;
      khiter_t iter = kh_put_modules(env.modulemap, (const char*)name.bytes, &r);
      if(!r)
        return ERR_FATAL_DUPLICATE_MODULE_NAME;

      kh_val(env.modulemap, iter) = i;
      break;
    }
    case TOKEN_INVOKE:
    case TOKEN_GET:
    {
      tokens.Pop();
      EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
      WatResult result;
      if(r = ParseWatScriptAction(env, tokens, mapping, last, cache, result))
        return r;
      EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
      break;
    }
    case TOKEN_ASSERT_TRAP:
      tokens.Pop();
      if(tokens.Size() > 1 && tokens[0].id == TOKEN_OPEN && tokens[1].id == TOKEN_MODULE) // Check if we're actually trapping on a module load
      {
        EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
        if(r = ParseWatScriptModule(env, tokens, mapping, last, cache))
          return r;
        EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);

        r = CompileScript(env, "wast.dll", cache);
        if(r != ERR_RUNTIME_TRAP)
          return ERR_RUNTIME_ASSERT_FAILURE;
      }
      else
      {
        EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
        WatResult result;
        r = ParseWatScriptAction(env, tokens, mapping, last, cache, result);
        if(r != ERR_RUNTIME_TRAP)
          return ERR_RUNTIME_ASSERT_FAILURE;
        EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
      }
      break;
    case TOKEN_ASSERT_RETURN:
    case TOKEN_ASSERT_RETURN_CANONICAL_NAN:
    case TOKEN_ASSERT_RETURN_ARITHMETIC_NAN:
    {
      Token t = tokens.Pop();
      EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
      WatResult result;
      if(r = ParseWatScriptAction(env, tokens, mapping, last, cache, result))
        return r;
      EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
      Instruction value;
      WatState state(*last);

      switch(t.id)
      {
      case TOKEN_ASSERT_RETURN:
        if(r = WatInitializer(state, tokens, value))
          return r;
        switch(value.opcode)
        {
        case OP_i32_const:
          if(result.type != TE_i32 || result.i32 != value.immediates[0]._varsint32)
            return ERR_RUNTIME_ASSERT_FAILURE;
          break;
        case OP_i64_const:
          if(result.type != TE_i64 || result.i64 != value.immediates[0]._varsint64)
            return ERR_RUNTIME_ASSERT_FAILURE;
          break;
        case OP_f32_const:
          if(result.type != TE_f32 || result.f32 != value.immediates[0]._float32)
            return ERR_RUNTIME_ASSERT_FAILURE;
          break;
        case OP_f64_const:
          if(result.type != TE_f64 || result.f64 != value.immediates[0]._float64)
            return ERR_RUNTIME_ASSERT_FAILURE;
          break;
        }
        break;
      case TOKEN_ASSERT_RETURN_ARITHMETIC_NAN:
      case TOKEN_ASSERT_RETURN_CANONICAL_NAN:
        if(result.type == TE_f32 && !WatIsNaN(result.f32, t.id == TOKEN_ASSERT_RETURN_CANONICAL_NAN))
          return ERR_RUNTIME_ASSERT_FAILURE;
        if(result.type == TE_f64 && !WatIsNaN(result.f64, t.id == TOKEN_ASSERT_RETURN_CANONICAL_NAN))
          return ERR_RUNTIME_ASSERT_FAILURE;
        break;
      }
      break;
    }
    case TOKEN_ASSERT_MALFORMED:
    case TOKEN_ASSERT_INVALID:
    case TOKEN_ASSERT_UNLINKABLE:
      tokens.Pop();
      EXPECTED(tokens, TOKEN_OPEN, ERR_WAT_EXPECTED_OPEN);
      r = ParseWatScriptModule(env, tokens, mapping, last, cache);
      if(r == ERR_SUCCESS) // prove compilation failed
        return ERR_RUNTIME_ASSERT_FAILURE;
      EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
      EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);

      break;
    case TOKEN_ASSERT_EXHAUSTION:
      assert(false);
      break;
    case TOKEN_SCRIPT:
    case TOKEN_INPUT:
    case TOKEN_OUTPUT:
    {
      SkipSection(tokens);
      EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
      break;
    }
    default:
      return ERR_WAT_EXPECTED_TOKEN;
    }

    EXPECTED(tokens, TOKEN_CLOSE, ERR_WAT_EXPECTED_CLOSE);
  }

  return ERR_SUCCESS;
}