// Copyright (c) 2020 Leedehai. All rights reserved.
// Use of this source code is governed under the LICENSE.txt file.
// Copyright 2015 The Chromium Authors. All rights reserved.
// License of Chromium: see CREDITS
// Copyright (c) 2008, Google Inc.
// License of glog: see CREDITS

#include <string.h>  // memchr(), memcmp(), memmove(), memset(), memcpy()

#include <algorithm>  // std::min()
#include <limits>  // std::numeric_limits<>

#include "common.h"
#include "sblz/sblz.h"

#if defined(OS_LINUX)

#include <stdlib.h>  // abort()
// System headers
#include <elf.h>  // Edhr
#include <errno.h>  // errno
#include <fcntl.h>  // O_RDONLY
#include <link.h>  // ElfW
#include <unistd.h>  // open()

#elif defined(OS_MACOS)

// System headers
#include <dlfcn.h>  // dladdr() (note the Linux version is slightly different)

#endif

namespace sblz {
namespace posix {

#if defined(OS_LINUX)

// Re-runs fn until it doesn't cause EINTR, which means that the function
// was interrupted by a signal before the function could finish its job.
#define NO_INTR(fn) \
  do {              \
  } while ((fn) < 0 && errno == EINTR)

// We don't use assert() since it's not guaranteed to be
// async-signal-safe.  Instead we define a minimal assertion
// macro. So far, we don't need pretty printing for __FILE__, etc.
// A wrapper for abort() to make it callable in `?:`.
static int Abort() {
  abort();
  return 0;  // Should not reach.
}
#define SAFE_ASSERT(expr) ((expr) ? 0 : Abort())

namespace {

// Thin wrapper around a file descriptor so that the file descriptor
// gets closed for sure.
struct FileDescriptor {
  const int fd_;
  explicit FileDescriptor(int fd) : fd_(fd) {}
  ~FileDescriptor() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }
  int get() { return fd_; }

 private:
  explicit FileDescriptor(const FileDescriptor&);
  void operator=(const FileDescriptor&);
};

// Read up to "count" bytes from "offset" in the file pointed by file
// descriptor "fd" into the buffer starting at "buf" while handling short reads
// and EINTR.  On success, return the number of bytes read.  Otherwise, return
// -1.
static ssize_t ReadFromOffset(const int fd,
                              void* buf,
                              const size_t count,
                              const off_t offset) {
  SAFE_ASSERT(fd >= 0);
  SAFE_ASSERT(count <= std::numeric_limits<ssize_t>::max());
  char* buf0 = reinterpret_cast<char*>(buf);
  ssize_t num_bytes = 0;
  while (num_bytes < count) {
    ssize_t len;
    NO_INTR(len = pread(fd, buf0 + num_bytes, count - num_bytes,
                        offset + num_bytes));
    if (len < 0) {  // There was an error other than EINTR.
      return -1;
    }
    if (len == 0) {  // Reached EOF.
      break;
    }
    num_bytes += len;
  }
  SAFE_ASSERT(num_bytes <= count);
  return num_bytes;
}

// Try reading exactly "count" bytes from "offset" bytes in a file
// pointed by "fd" into the buffer starting at "buf" while handling
// short reads and EINTR.  On success, return true. Otherwise, return
// false.
static bool ReadFromOffsetExact(const int fd,
                                void* buf,
                                const size_t count,
                                const off_t offset) {
  ssize_t len = ReadFromOffset(fd, buf, count, offset);
  return len == count;
}

// Helper class for reading lines from file.
//
// Note: we don't use ProcMapsIterator since the object is big (it has
// a 5k array member) and uses async-unsafe functions such as sscanf()
// and snprintf().
class LineReader {
 public:
  explicit LineReader(int fd, char* buf, int buf_len, off_t offset)
      : fd_(fd),
        buf_(buf),
        buf_len_(buf_len),
        offset_(offset),
        bol_(buf),
        eol_(buf),
        eod_(buf) {}

  // Read '\n'-terminated line from file.  On success, modify "bol"
  // and "eol", then return true.  Otherwise, return false.
  //
  // Note: if the last line doesn't end with '\n', the line will be
  // dropped.  It's an intentional behavior to make the code simple.
  bool ReadLine(const char** bol, const char** eol) {
    if (BufferIsEmpty()) {  // First time.
      const ssize_t num_bytes = ReadFromOffset(fd_, buf_, buf_len_, offset_);
      if (num_bytes <= 0) {  // EOF or error.
        return false;
      }
      offset_ += num_bytes;
      eod_ = buf_ + num_bytes;
      bol_ = buf_;
    } else {
      bol_ = eol_ + 1;  // Advance to the next line in the buffer.
      SAFE_ASSERT(bol_ <= eod_);  // "bol_" can point to "eod_".
      if (!HasCompleteLine()) {
        const int incomplete_line_length = eod_ - bol_;
        // Move the trailing incomplete line to the beginning.
        memmove(buf_, bol_, incomplete_line_length);
        // Read text from file and append it.
        char* const append_pos = buf_ + incomplete_line_length;
        const int capacity_left = buf_len_ - incomplete_line_length;
        const ssize_t num_bytes =
            ReadFromOffset(fd_, append_pos, capacity_left, offset_);
        if (num_bytes <= 0) {  // EOF or error.
          return false;
        }
        offset_ += num_bytes;
        eod_ = append_pos + num_bytes;
        bol_ = buf_;
      }
    }
    eol_ = FindLineFeed();
    if (eol_ == NULL) {  // '\n' not found.  Malformed line.
      return false;
    }
    *eol_ = '\0';  // Replace '\n' with '\0'.

    *bol = bol_;
    *eol = eol_;
    return true;
  }

  // Beginning of line.
  const char* bol() { return bol_; }

  // End of line.
  const char* eol() { return eol_; }

 private:
  explicit LineReader(const LineReader&);
  void operator=(const LineReader&);

  char* FindLineFeed() {
    return reinterpret_cast<char*>(memchr(bol_, '\n', eod_ - bol_));
  }

  bool BufferIsEmpty() { return buf_ == eod_; }

  bool HasCompleteLine() { return !BufferIsEmpty() && FindLineFeed() != NULL; }

  const int fd_;
  char* const buf_;
  const int buf_len_;
  off_t offset_;
  char* bol_;
  char* eol_;
  const char* eod_;  // End of data in "buf_".
};

// Place the hex number read from "start" into "*hex".  The pointer to
// the first non-hex character or "end" is returned.
static char* GetHex(const char* start, const char* end, uint64_t* hex) {
  *hex = 0;
  const char* p;
  for (p = start; p < end; ++p) {
    int ch = *p;
    if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') ||
        (ch >= 'a' && ch <= 'f')) {
      *hex = (*hex << 4) | (ch < 'A' ? ch - '0' : (ch & 0xF) + 9);
    } else {  // Encountered the first non-hex character.
      break;
    }
  }
  SAFE_ASSERT(p <= end);
  return const_cast<char*>(p);
}

// Returns elf_header.e_type if the file pointed by fd is an ELF binary.
// ET_NONE         An unknown type.
// ET_REL          A relocatable file.
// ET_EXEC         An executable file.
// ET_DYN          A shared object.
// ET_CORE         A core file.
static int FileGetElfType(const int fd) {
  ElfW(Ehdr) elf_header;
  if (!ReadFromOffsetExact(fd, &elf_header, sizeof(elf_header), 0)) {
    return -1;
  }
  if (memcmp(elf_header.e_ident, ELFMAG, SELFMAG) != 0) {
    return -1;
  }
  return elf_header.e_type;
}

// POSIX doesn't define any async-signal safe function for converting
// an integer to ASCII. We'll have to define our own version.
// itoa_r() converts a (signed) integer to ASCII. It returns "buf", if the
// conversion was successful or NULL otherwise. It never writes more than "sz"
// bytes. Output will be truncated as needed, and a NUL character is always
// appended.
// NOTE: code from sandbox/linux/seccomp-bpf/demo.cc.
static char* itoa_r(intptr_t i,
                    char* buf,
                    size_t sz,
                    int base,
                    size_t padding) {
  // Make sure we can write at least one NUL byte.
  size_t n = 1;
  if (n > sz)
    return NULL;

  if (base < 2 || base > 16) {
    buf[0] = '\000';
    return NULL;
  }

  char* start = buf;

  uintptr_t j = i;

  // Handle negative numbers (only for base 10).
  if (i < 0 && base == 10) {
    // This does "j = -i" while avoiding integer overflow.
    j = static_cast<uintptr_t>(-(i + 1)) + 1;

    // Make sure we can write the '-' character.
    if (++n > sz) {
      buf[0] = '\000';
      return NULL;
    }
    *start++ = '-';
  }

  // Loop until we have converted the entire number. Output at least one
  // character (i.e. '0').
  char* ptr = start;
  do {
    // Make sure there is still enough space left in our output buffer.
    if (++n > sz) {
      buf[0] = '\000';
      return NULL;
    }

    // Output the next digit.
    *ptr++ = "0123456789abcdef"[j % base];
    j /= base;

    if (padding > 0)
      padding--;
  } while (j > 0 || padding > 0);

  // Terminate the output with a NUL character.
  *ptr = '\000';

  // Conversion to ASCII actually resulted in the digits being in reverse
  // order. We can't easily generate them in forward order, as we can't tell
  // the number of characters needed until we are done converting.
  // So, now, we reverse the string (except for the possible "-" sign).
  while (--ptr > start) {
    char ch = *ptr;
    *ptr = *start;
    *start++ = ch;
  }
  return buf;
}

// Safely appends string |source| to string |dest|. Never writes past the
// buffer size |dest_size| and guarantees that |dest| is null-terminated.
static void SafeAppendString(const char* source, char* dest, int dest_size) {
  int dest_string_length = strlen(dest);
  SAFE_ASSERT(dest_string_length < dest_size);
  dest += dest_string_length;
  dest_size -= dest_string_length;
  strncpy(dest, source, dest_size);
  // Making sure |dest| is always null-terminated.
  dest[dest_size - 1] = '\0';
}

// Converts a 64-bit value into a hex string, and safely appends it to |dest|.
// Never writes past the buffer size |dest_size| and guarantees that |dest| is
// null-terminated.
static void SafeAppendHexNumber(uint64_t value, char* dest, int dest_size) {
  // 64-bit numbers in hex can have up to 16 digits.
  char buf[17] = {'\0'};
  SafeAppendString(itoa_r(value, buf, sizeof(buf), 16, 0), dest, dest_size);
}

void WriteAddressNumber(void* address,
                        uint64_t base_addr,
                        char* buffer,
                        size_t buffer_size) {
  SafeAppendString("+0x", buffer, buffer_size);
  SafeAppendHexNumber(reinterpret_cast<uint64_t>(address) - base_addr, buffer,
                      buffer_size);
  buffer[buffer_size - 1] = '\0';  // Make sure it is always '\0'-terminated.
}

}  // namespace

// Read the section headers in the given ELF binary, and if a section
// of the specified type is found, set the output to this section header
// and return true. Otherwise, return false.
// To keep stack consumption low, we would like this function to not get
// inlined.
static bool GetSectionHeaderByType(const int fd,
                                   ElfW(Half) sh_num,
                                   const off_t sh_offset,
                                   ElfW(Word) type,
                                   ElfW(Shdr) * buffer) {
  // Read at most 16 section headers at a time to save read calls.
  ElfW(Shdr) buf[16];
  for (int i = 0; i < sh_num;) {
    const ssize_t num_bytes_left = (sh_num - i) * sizeof(buf[0]);
    const ssize_t num_bytes_to_read =
        (sizeof(buf) > num_bytes_left) ? num_bytes_left : sizeof(buf);
    const ssize_t len = ReadFromOffset(fd, buf, num_bytes_to_read,
                                       sh_offset + i * sizeof(buf[0]));
    if (len == -1) {
      return false;
    }
    SAFE_ASSERT(len % sizeof(buf[0]) == 0);
    const ssize_t num_headers_in_buf = len / sizeof(buf[0]);
    SAFE_ASSERT(num_headers_in_buf <= sizeof(buf) / sizeof(buf[0]));
    for (int j = 0; j < num_headers_in_buf; ++j) {
      if (buf[j].sh_type == type) {
        *buffer = buf[j];
        return true;
      }
    }
    i += num_headers_in_buf;
  }
  return false;
}

// Read a symbol table and look for the symbol containing the
// pc. Iterate over symbols in a symbol table and look for the symbol
// containing "pc". On success, return true and write the symbol name
// to buffer. Otherwise, return false.
// To keep stack consumption low, we would like this function to not get
// inlined.
bool FindSymbol(uint64_t pc,
                const int fd,
                char* buffer,
                int buffer_size,
                uint64_t symbol_offset,
                const ElfW(Shdr) * strtab,
                const ElfW(Shdr) * symtab) {
  if (symtab == NULL) {
    return false;
  }
  const int num_symbols = symtab->sh_size / symtab->sh_entsize;
  for (int i = 0; i < num_symbols;) {
    off_t offset = symtab->sh_offset + i * symtab->sh_entsize;

    // If we are reading Elf64_Sym's, we want to limit this array to
    // 32 elements (to keep stack consumption low), otherwise we can
    // have a 64 element Elf32_Sym array.
#if __WORDSIZE == 64
#define NUM_SYMBOLS 32
#else
#define NUM_SYMBOLS 64
#endif

    // Read at most NUM_SYMBOLS symbols at once to save read() calls.
    ElfW(Sym) buf[NUM_SYMBOLS];
    int num_symbols_to_read = std::min(NUM_SYMBOLS, num_symbols - i);
    const ssize_t len =
        ReadFromOffset(fd, &buf, sizeof(buf[0]) * num_symbols_to_read, offset);
    SAFE_ASSERT(len % sizeof(buf[0]) == 0);
    const ssize_t num_symbols_in_buf = len / sizeof(buf[0]);
    SAFE_ASSERT(num_symbols_in_buf <= num_symbols_to_read);
    for (int j = 0; j < num_symbols_in_buf; ++j) {
      const ElfW(Sym)& symbol = buf[j];
      uint64_t start_address = symbol.st_value;
      start_address += symbol_offset;
      uint64_t end_address = start_address + symbol.st_size;
      if (symbol.st_value != 0 &&  // Skip null value symbols.
          symbol.st_shndx != 0 &&  // Skip undefined symbols.
          start_address <= pc && pc < end_address) {
        ssize_t len1 = ReadFromOffset(fd, buffer, buffer_size,
                                      strtab->sh_offset + symbol.st_name);
        if (len1 <= 0 || memchr(buffer, '\0', buffer_size) == NULL) {
          memset(buffer, 0, buffer_size);
          return false;
        }
        return true;  // Obtained the symbol name.
      }
    }
    i += num_symbols_in_buf;
  }
  return false;
}

// Find the object file that contains the given program counter (pc) value.
// If found, sets `start_address` to the start address of where this object
// file is mapped in memory, sets the module base address into `base_address`,
// copies the object file name into `obj_filename_buffer`, and attempts to
// open the object file. If the object file is opened successfully, returns
// the file descriptor. Otherwise, returns -1.
// See http://man7.org/linux/man-pages/man5/proc.5.html for introduction.
static int FindAndOpenObjectFileWithProgramCounter(uint64_t pc,
                                                   uint64_t* start_address,
                                                   uint64_t* base_address,
                                                   char* obj_filename_buffer,
                                                   int buffer_size) {
  int object_fd;

  int maps_fd;
  NO_INTR(maps_fd = open("/proc/self/maps", O_RDONLY));
  FileDescriptor wrapped_maps_fd(maps_fd);
  if (wrapped_maps_fd.get() < 0) {
    return -1;
  }

  int mem_fd;
  NO_INTR(mem_fd = open("/proc/self/mem", O_RDONLY));
  FileDescriptor wrapped_mem_fd(mem_fd);
  if (wrapped_mem_fd.get() < 0) {
    return -1;
  }

  // Iterate over maps and look for the map containing the pc.  Then
  // look into the symbol tables inside.
  char buf[1024];  // Big enough for line of sane /proc/self/maps
  int num_maps = 0;
  LineReader reader(wrapped_maps_fd.get(), buf, sizeof(buf), 0);
  while (true) {
    num_maps++;
    const char* cursor;
    const char* eol;
    if (!reader.ReadLine(&cursor, &eol)) {  // EOF or malformed line.
      return -1;
    }

    // Start parsing line in /proc/self/maps.  Here is an example:
    //
    // 08048000-0804c000 r-xp 00000000 08:01 2142121    /bin/cat
    //
    // We want start address (08048000), end address (0804c000), flags
    // (r-xp) and file name (/bin/cat).

    // Read start address.
    cursor = GetHex(cursor, eol, start_address);
    if (cursor == eol || *cursor != '-') {
      return -1;  // Malformed line.
    }
    ++cursor;  // Skip '-'.

    // Read end address.
    uint64_t end_address;
    cursor = GetHex(cursor, eol, &end_address);
    if (cursor == eol || *cursor != ' ') {
      return -1;  // Malformed line.
    }
    ++cursor;  // Skip ' '.

    // Read flags.  Skip flags until we encounter a space or eol.
    const char* const flags_start = cursor;
    while (cursor < eol && *cursor != ' ') {
      ++cursor;
    }
    // We expect at least four letters for flags (ex. "r-xp").
    if (cursor == eol || cursor < flags_start + 4) {
      return -1;  // Malformed line.
    }

    // Determine the base address by reading ELF headers in process memory.
    ElfW(Ehdr) ehdr;
    // Skip non-readable maps.
    if (flags_start[0] == 'r' &&
        ReadFromOffsetExact(mem_fd, &ehdr, sizeof(ElfW(Ehdr)),
                            *start_address) &&
        memcmp(ehdr.e_ident, ELFMAG, SELFMAG) == 0) {
      switch (ehdr.e_type) {
        case ET_EXEC:
          *base_address = 0;
          break;
        case ET_DYN:
          // Find the segment containing file offset 0. This will correspond
          // to the ELF header that we just read. Normally this will have
          // virtual address 0, but this is not guaranteed. We must subtract
          // the virtual address from the address where the ELF header was
          // mapped to get the base address.
          //
          // If we fail to find a segment for file offset 0, use the address
          // of the ELF header as the base address.
          *base_address = *start_address;
          for (unsigned i = 0; i != ehdr.e_phnum; ++i) {
            ElfW(Phdr) phdr;
            if (ReadFromOffsetExact(
                    mem_fd, &phdr, sizeof(phdr),
                    *start_address + ehdr.e_phoff + i * sizeof(phdr)) &&
                phdr.p_type == PT_LOAD && phdr.p_offset == 0) {
              *base_address = *start_address - phdr.p_vaddr;
              break;
            }
          }
          break;
        default:
          // ET_REL or ET_CORE. These aren't directly executable, so they don't
          // affect the base address.
          break;
      }
    }

    // Check start and end addresses.
    if (!(*start_address <= pc && pc < end_address)) {
      continue;  // We skip this map.  PC isn't in this map.
    }

    // Check flags.  We are only interested in "r*x" maps.
    if (flags_start[0] != 'r' || flags_start[2] != 'x') {
      continue;  // We skip this map.
    }
    ++cursor;  // Skip ' '.

    // Read file offset.
    uint64_t file_offset;
    cursor = GetHex(cursor, eol, &file_offset);
    if (cursor == eol || *cursor != ' ') {
      return -1;  // Malformed line.
    }
    ++cursor;  // Skip ' '.

    // Skip to file name.  "cursor" now points to dev.  We need to
    // skip at least two spaces for dev and inode.
    int num_spaces = 0;
    while (cursor < eol) {
      if (*cursor == ' ') {
        ++num_spaces;
      } else if (num_spaces >= 2) {
        // The first non-space character after skipping two spaces
        // is the beginning of the file name.
        break;
      }
      ++cursor;
    }
    if (cursor == eol) {
      return -1;  // Malformed line.
    }

    // Finally, "cursor" now points to file name of our interest.
    NO_INTR(object_fd = open(cursor, O_RDONLY));
    if (object_fd < 0) {
      // Failed to open object file.  Copy the object file name to
      // |obj_filename_buffer|.
      strncpy(obj_filename_buffer, cursor, buffer_size);
      // Making sure |obj_filename_buffer| is always null-terminated.
      obj_filename_buffer[buffer_size - 1] = '\0';
      return -1;
    }
    return object_fd;
  }
}

// Get the symbol name of "pc" from the file pointed by "fd". Process
// both regular and dynamic symbol tables if necessary. On success,
// write the symbol name to "buffer" and return true. Otherwise, return
// false.
static bool GetSymbolFromObjectFile(const int fd,
                                    uint64_t pc,
                                    char* buffer,
                                    int buffer_size,
                                    uint64_t base_address) {
  // Read the ELF header.
  ElfW(Ehdr) elf_header;
  if (!ReadFromOffsetExact(fd, &elf_header, sizeof(elf_header), 0)) {
    return false;
  }

  ElfW(Shdr) symtab, strtab;

  // Consult a regular symbol table first.
  if (GetSectionHeaderByType(fd, elf_header.e_shnum, elf_header.e_shoff,
                             SHT_SYMTAB, &symtab)) {
    if (!ReadFromOffsetExact(
            fd, &strtab, sizeof(strtab),
            elf_header.e_shoff + symtab.sh_link * sizeof(symtab))) {
      return false;
    }
    if (FindSymbol(pc, fd, buffer, buffer_size, base_address, &strtab,
                   &symtab)) {
      return true;  // Found the symbol in a regular symbol table.
    }
  }

  // If the symbol is not found, then consult a dynamic symbol table.
  if (GetSectionHeaderByType(fd, elf_header.e_shnum, elf_header.e_shoff,
                             SHT_DYNSYM, &symtab)) {
    if (!ReadFromOffsetExact(
            fd, &strtab, sizeof(strtab),
            elf_header.e_shoff + symtab.sh_link * sizeof(symtab))) {
      return false;
    }
    if (FindSymbol(pc, fd, buffer, buffer_size, base_address, &strtab,
                   &symtab)) {
      return true;  // Found the symbol in a dynamic symbol table.
    }
  }

  return false;
}

EXPORT bool Symbolize(void* address, char* buffer, size_t buffer_size) {
  uint64_t start_addr = 0;
  uint64_t base_addr = 0;

  if (buffer_size < 5) {
    return false;
  }
  buffer[0] = '\0';

  int object_file_fd = FindAndOpenObjectFileWithProgramCounter(
      reinterpret_cast<uint64_t>(address), &start_addr, &base_addr, buffer + 1,
      buffer_size - 1);

  if (object_file_fd < 0) {
    // The object file containing PC was determined successfully however not
    // opened. This is still considered success and we write the instruction
    // address.
    WriteAddressNumber(address, base_addr, buffer, buffer_size);
    return true;
  }

  FileDescriptor wrapped_object_fd(object_file_fd);
  int elf_type = FileGetElfType(wrapped_object_fd.get());
  if (elf_type == -1) {
    return false;
  }

  bool got_symbol = GetSymbolFromObjectFile(wrapped_object_fd.get(),
                                            reinterpret_cast<uint64_t>(address),
                                            buffer, buffer_size, base_addr);
  buffer[buffer_size - 1] = '\0';  // Make sure it is always '\0'-terminated.

  if (!got_symbol) {
    // The object file containing PC was opened successfully however the
    // symbol was not found. The object may have been stripped, but this is
    // still considered success and we write the instruction address.
    WriteAddressNumber(address, base_addr, buffer, buffer_size);
  }

  return true;
}

#elif defined(OS_MACOS)

EXPORT bool Symbolize(void* address, char* buffer, size_t buffer_size) {
  Dl_info info;
  // If an image containing addr cannot be found, dladdr() returns 0. On success
  // it returns a non-zero value.
  // https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/dladdr.3.html
  if (dladdr(address, &info) && info.dli_sname) {
    // If the buffer is not large enough, we still copy the symbol to it, though
    // the copied content would be incomplete and would be not demangle-able.
    memcpy(buffer, info.dli_sname, buffer_size);
    buffer[buffer_size - 1] = '\0';
    return true;
  }
  return false;
}

#endif

}  // namespace posix
}  // namespace sblz
