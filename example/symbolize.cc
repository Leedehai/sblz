// Copyright (c) 2020 Leedehai. All rights reserved.
// Use of this source code is governed under the LICENSE.txt file.

// macOS (Darwin 19.4.0 on xnu-6153.101.6, Apple Clang 11.0.0, libc++ 800.7.0)
// [11] 0x0000000106ec114e _ZN10StackTraceC2Ev
// [10] 0x0000000106ec0ba5 _ZN10StackTraceC1Ev
// [09] 0x0000000106ec0965 _Z2f7v
// [08] 0x0000000106ec1019 _Z2f6v
// [07] 0x0000000106ec1031 _Z2f5PKNSt3__113basic_ostreamIcNS_11char_traitsIcEEEE
// [06] 0x0000000106ec105b _Z2f4PFvvE
// [05] 0x0000000106ec1099 _Z2f3iiiiid
// [04] 0x0000000106ec10d4 _Z2f2f8MyStruct
// [03] 0x0000000106ec10fb _Z2f1ii
// [02] 0x0000000106ec1120 main
// [01] 0x00007fff6fe85cc9 start
// [00] 0x0000000000000001 (blank)

// Ubuntu (Linux 4.15.0-96, Clang 10.0.0, libstdc++ 6.0.25)
// [10] 0x00000000004024b1 _ZN10StackTraceC2Ev
// [09] 0x0000000000401ec7 _Z2f7v
// [08] 0x0000000000402369 _Z2f6v
// [07] 0x0000000000402381 _Z2f5PKSo
// [06] 0x00000000004023ab _Z2f4PFvvE
// [05] 0x00000000004023ec _Z2f3iiiiid
// [04] 0x0000000000402434 _Z2f2f8MyStruct
// [03] 0x000000000040245b _Z2f1ii
// [02] 0x0000000000402480 main
// [01] 0x00007f8f22f5fb97 __libc_start_main
// [00] 0x0000000000400d8a _start

#include <execinfo.h>  // backtrace()
#include <string.h>  // memcpy()

#include <cstddef>
#include <iomanip>
#include <iostream>
#include <string>

#include "sblz/sblz.h"

#define NO_INLINE __attribute__((noinline))

bool itoa_r(intptr_t value,
            char* buffer,
            size_t buffer_size,
            int base,
            size_t min_width_of_digits,
            char padding_char);

class StackTrace {
 public:
  StackTrace() { count_ = backtrace(stack_trace_, kMaxStackTrace); }
  void** GetTrace() { return stack_trace_; }
  int GetCount() const { return count_; }

 private:
  static const int kMaxStackTrace = 32;
  void* stack_trace_[kMaxStackTrace];
  int count_;
};

NO_INLINE void f7() {
  StackTrace stack_trace;
  int trace_count = stack_trace.GetCount();
  void** trace = stack_trace.GetTrace();
  for (int i = 0; i < trace_count; ++i) {
    char symbol_buffer[128] = {0};
    if (!sblz::posix::Symbolize(trace[i], symbol_buffer,
                                sizeof(symbol_buffer))) {
      memcpy(symbol_buffer, "(blank)", sizeof(symbol_buffer));
    }
    char address_buffer[17] = {0};
    itoa_r(reinterpret_cast<intptr_t>(trace[i]), address_buffer,
           sizeof(address_buffer), /*base=*/16, /*min_width_of_digits=*/16,
           '0');
    std::cout << "[" << std::setfill('0') << std::setw(2)
              << (trace_count - i - 1) << "] 0x" << address_buffer << " "
              << symbol_buffer << std::endl;
  }
}

NO_INLINE void f6() {
  f7();
}

NO_INLINE void f5(const std::ostream*) {
  f6();
}

NO_INLINE void f4(void (*)()) {
  f5(&std::cout);
}

NO_INLINE void f3(int, int, int, int, int, double) {
  f4(f6);
}

class MyStruct {};

NO_INLINE void f2(float, MyStruct) {
  f3(0, 1, 2, 3, 4, 5.0);
}

NO_INLINE void f1(int, int) {
  f2(1.0, MyStruct());
}

int main() {
  f1(0, 1);
}

bool itoa_r(intptr_t value,
            char* buffer,
            size_t buffer_size,
            int base,
            size_t min_width_of_digits,
            char padding_char) {
  if (buffer_size < 1 || base < 2 || base > 16 ||
      min_width_of_digits >= buffer_size) {
    return false;
  }
  size_t used_bytes = 0;
  char* start = buffer;

  uintptr_t absolute_value;
  if (value >= 0) {
    absolute_value = value;
  } else {  // Negative value.
    // This does "working_value = |value|" but avoiding integer overflow.
    absolute_value = static_cast<uintptr_t>(-(value + 1)) + 1;

    if (buffer_size < used_bytes + 1) {
      return false;
    }
    *start++ = '-';
    ++used_bytes;
  }

  size_t& possible_slots_to_pad = min_width_of_digits;
  bool is_padding = false;
  char* p = start;
  // Generate the string in reverse order, because it is hard to do
  // so in a forward order as we can't predict how many characters
  // are needed to represent the value.
  do {
    if (buffer_size < used_bytes + 1) {
      return false;
    }

    if (is_padding) {
      *p++ = padding_char;
    } else {
      *p++ = "0123456789abcdef"[absolute_value % base];
    }
    absolute_value /= base;

    ++used_bytes;
    if (possible_slots_to_pad) {
      --possible_slots_to_pad;
    }

    if (absolute_value == 0) {
      if (possible_slots_to_pad <= 0) {
        break;
      } else {
        is_padding = true;
      }
    }
  } while (true);
  *p = '\0';

  // So, now, we reverse the string.
  --p;  // Now |ptr| points to the rightmost non-'\0'.
  while (start < p) {
    *start = *p ^ *start;
    *p = *p ^ *start;
    *start = *p ^ *start;
    ++start;
    --p;
  }
  return true;
}
