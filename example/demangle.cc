// Copyright (c) 2020 Leedehai. All rights reserved.
// Use of this source code is governed under the LICENSE.txt file.
// -----
// sblz::itanium::Demangle() just implements part of the demangling routine:
// argument types are not extracted.

#include <iostream>

#include "sblz/sblz.h"

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "[Error] expect 1 argument: the mangled symbol." << std::endl;
    return 1;
  }
  const char* mangled_symbol = argv[1];
  char buffer[512] = {0};
  const bool ok =
      sblz::itanium::Demangle(mangled_symbol, buffer, sizeof(buffer));
  std::cout << (ok ? buffer : mangled_symbol) << std::endl;
  return 0;  // Like c++filt, exit with 0 no matter what.
}
