// Copyright (c) 2020 Leedehai. All rights reserved.
// Use of this source code is governed under the LICENSE.txt file.

#include <execinfo.h>  // backtrace()

#include <cstddef>
#include <iomanip>
#include <iostream>

#include "sblz/sblz.h"

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

void f7() {
  StackTrace stack_trace;
  int trace_count = stack_trace.GetCount();
  void** trace = stack_trace.GetTrace();
  for (int i = 0; i < trace_count; ++i) {
    char symbol_buffer[32];
    sblz::posix::Symbolize(trace[i], symbol_buffer, sizeof(symbol_buffer));
    char address_buffer[17] = {0};
    itoa_r(reinterpret_cast<intptr_t>(trace[i]), address_buffer,
           sizeof(address_buffer), /*base=*/16, /*min_width_of_digits=*/16,
           '.');
    std::cout << "[" << std::setfill('0') << std::setw(2)
              << (trace_count - i - 1) << "] 0x" << address_buffer << " "
              << symbol_buffer << std::endl;
  }
}

void f6() {
  f7();
}

void f5() {
  f6();
}

void f4() {
  f5();
}

void f3() {
  f4();
}

void f2() {
  f3();
}

void f1() {
  f2();
}

int main() {
  f1();
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
