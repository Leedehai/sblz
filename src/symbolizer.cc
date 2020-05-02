// Copyright (c) 2020 Leedehai. All rights reserved.
// Use of this source code is governed under the LICENSE.txt file.

#include <string.h>

#include "macro.h"
#include "sblz/sblz.h"

#if defined(OS_LINUX)
#elif defined(OS_MACOS)
#include <dlfcn.h>  // dladdr() (note the Linux version is slightly different)
#endif

namespace sblz {
namespace posix {

#if defined(OS_LINUX)

#elif defined(OS_MACOS)

EXPORT bool Symbolize(void* address, char* buffer, size_t buffer_size) {
  Dl_info info;
  if (dladdr(address, &info) && strlen(info.dli_sname) < buffer_size) {
    strcpy(buffer, info.dli_sname);
    return true;
  }
  return false;
}

#endif
}  // namespace posix
}  // namespace sblz
