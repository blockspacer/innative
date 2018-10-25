// Copyright �2018 Black Sphere Studios
// For conditions of distribution and use, see copyright notice in innative.h

#include "innative/export.h"

#ifdef IR_PLATFORM_WIN32
#pragma pack(push)
#pragma pack(8)
#define WINVER 0x0501 //_WIN32_WINNT_WINXP   
#define _WIN32_WINNT 0x0501
#define NTDDI_VERSION 0x05010300 //NTDDI_WINXPSP3 
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX // Some compilers enable this by default
#define NOMINMAX
#endif
#define NODRAWTEXT
#define NOBITMAP
#define NOMCX
#define NOSERVICE
#define NOHELP
#define NOGDI
#include <windows.h>
#pragma pack(pop)
#elif defined(IR_PLATFORM_POSIX)
#include <unistd.h>
#include <sys/mman.h>
#else
#error unknown platform!
#endif

IR_COMPILER_DLLEXPORT extern void _innative_internal_env_memcpy(char* dest, const char* src, uint64_t sz)
{
  // Very simple memcpy implementation because we don't have access to the C library
  while(sz > sizeof(uint64_t))
  {
    *((uint64_t*)dest) = *((uint64_t*)src);
    dest += sizeof(uint64_t);
    src += sizeof(uint64_t);
    sz -= sizeof(uint64_t);
  }
  while(sz)
  {
    *dest = *src;
    dest += 1;
    src += 1;
    sz -= 1;
  }
}

#ifdef IR_PLATFORM_WIN32
HANDLE heap = 0;
#elif defined(IR_PLATFORM_POSIX)
#ifdef IR_CPU_x86_64
IR_COMPILER_NAKED void* _innative_syscall(size_t syscall_number, const void* p1, size_t p2, size_t p3, size_t p4, size_t p5)
{
  __asm volatile(
    "movq %rdi, %rax\n\t"
    "movq %rsi, %rdi\n\t"
    "movq %rdx, %rsi\n\t"
    "movq %rcx, %rdx\n\t"
    "movq %r8, %r10\n\t"
    "movq %r9, %r8\n\t"
    "movq $0, %r9\n\t"
    "syscall");
}

const int SYSCALL_WRITE = 1;
const int SYSCALL_MMAP = 9;
const int SYSCALL_MREMAP = 25;
const int SYSCALL_EXIT = 60;
#else
#error unsupported architecture!
#endif
#endif

// Platform-specific implementation of the mem.grow instruction, except it works in bytes
IR_COMPILER_DLLEXPORT extern void* _innative_internal_env_grow_memory(void* p, uint64_t i, uint64_t max)
{
  uint64_t* info = (uint64_t*)p;
  if(info != 0)
  {
    i += info[-1];
    if(max > 0 && i > max)
      return 0;
#ifdef IR_PLATFORM_WIN32
    info = HeapReAlloc(heap, HEAP_ZERO_MEMORY, info - 1, i + sizeof(uint64_t));
#elif defined(IR_PLATFORM_POSIX)
    info = _innative_syscall(SYSCALL_MREMAP, p, info[-1] + sizeof(uint64_t), i + sizeof(uint64_t), PROT_READ | PROT_WRITE);
#else
#error unknown platform!
#endif
  }
  else if(!max || i <= max)
  {
#ifdef IR_PLATFORM_WIN32
    if(!heap)
      heap = HeapCreate(0, i, 0);
    info = HeapAlloc(heap, HEAP_ZERO_MEMORY, i + sizeof(uint64_t));
#elif defined(IR_PLATFORM_POSIX)
    info = _innative_syscall(SYSCALL_MMAP, NULL, i + sizeof(uint64_t), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1);
#else
#error unknown platform!
#endif
  }

  if(!info)
    return 0;
  info[0] = i;
  return info + 1;
}

// You cannot return from the entry point of a program, you must instead call a platform-specific syscall to terminate it.
IR_COMPILER_DLLEXPORT extern void _innative_internal_env_exit(int status)
{
#ifdef IR_PLATFORM_WIN32
  ExitProcess(status);
#elif defined(IR_PLATFORM_POSIX)
  _innative_syscall(SYSCALL_EXIT, status, 0, 0, 0, 0);
#endif
}

void _innative_internal_write_out(const void* buf, size_t num)
{
#ifdef IR_PLATFORM_WIN32
  DWORD out;
  WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), buf, num, &out, NULL);
#elif defined(IR_PLATFORM_POSIX)
  _innative_syscall(SYSCALL_WRITE, (void*)1, (size_t)buf, num, 0, 0);
#else
#error unknown platform!
#endif
}

static char lookup[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

IR_COMPILER_DLLEXPORT extern void _innative_internal_env_print(uint64_t a)
{
  char buf[25] = { 0 };

  int i = 0;
  do
  {
    buf[i++] = lookup[a >> 60];
    a <<= 4;
  } while(i < 16);
  buf[i++] = '\n';
  _innative_internal_write_out(buf, i);
}

IR_COMPILER_DLLEXPORT extern void _innative_internal_env_print_compiler(uint64_t a)
{
  _innative_internal_env_print(a);
}

IR_COMPILER_DLLEXPORT extern void _innative_internal_env_memdump(const unsigned char* mem, uint64_t sz)
{
  static char prefix[] = "\n --- MEMORY DUMP ---\n\n";
  char buf[256];

  _innative_internal_write_out(prefix, sizeof(prefix));
  for(uint64_t i = 0; i < sz;)
  {
    uint64_t j;
    for(j = 0; j < 128 && i < sz; ++j, ++i)
    {
      buf[j * 2] = lookup[(mem[i] & 0xF0) >> 4];
      buf[j * 2 + 1] = lookup[mem[i] & 0x0F];
    }
    _innative_internal_write_out(buf, j * 2);
  }
}
