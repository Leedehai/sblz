# SBLZ
> Async-Signal Safe Symbolizer and Demangler for Itanium C++ ABI.

The symbolizer and demangler are used to interpret stack traces produced by
POSIX system call `backtrace()` so as to make them human-readable.

[CREDITS](CREDITS): Basically it is a refactoring and simplification of
[Chromium](https://www.chromium.org)'s extraction of
[glog](https://github.com/google/glog) so that it can be embedded easily.

> Why not use `backtrace_symbols()` and `abi::__cxa_demangle()`?<br><br>Because
they use dynamic memory allocation under the hood, and are thus
[async-signal unsafe](http://man7.org/linux/man-pages/man7/signal-safety.7.html),
making them not appropriate for signal handlers. Often times, signal handlers
are where symbolizing and demangling are needed (to produce a stack trace).<br>
An interesting issue on Chromium: [crbug.com/101155](http://crbug.com/101155),
filed in October 2011.<br>In fact, the aforementioned `backtrace()` call is
async-signal unsafe, too, but there are ways to mitigate it (e.g. Chromium
commit [1e218b79cc](https://chromium.googlesource.com/chromium/src.git/+/1e218b79cc)
which solved that issue).

## Prerequisites

- operating system: Linux or macOS
- compiler: [Clang](https://clang.llvm.org) compiler (though GCC is supported
with best efforts) supporting C++17.
- [GNU Make](https://www.gnu.org/software/make/)

## How to use

APIs: see [include/sblz/sblz.h](include/sblz/sblz.h).

**Symbolizer**

The symbolizer walks the call stack of the program under inspection, so you need
to link the symbolizer into that program binary. See [Makefile](Makefile) for
how to build and [this example](example/symbolize.cc) for how to use it in a
client program.

## How to test

```sh
# Build the binaries.
make clean && make

# Symbolizer
tests/check_symbolizer.py

# Demangler
TODO
```

## How to use

This project must be compiled as an object, because it must be running in the
client program's process in order to read its symbols.

APIs:
```c++
// Get symbols from function return addresses
TODO

// Demangle a symbol
char buffer[64] = {0}; // Don't use dynamic memory allocation, as it is async-signal unsafe
TODO
```

## Concepts

###  Async-signal safety

The [man page of Linux](http://man7.org/linux/man-pages/man7/signal-safety.7.html)
gives a very clear description of this concept.

Basically, async-signal unsafe functions cannot be used in a signal handler.
That means a large number of common library constructs should be avoided,
including those which need dynamic memory allocation (`malloc()`, `new`, etc.),
such as `std::vector`, `std::string`, `std::ostream`, etc.

### Symbolizing

The POSIX system call `backtrace()` returns a series of addresses, each of which
is a function return address, populated at each stack frame. However, the
address values in their own is not very informative. Users need the function
names to figure out the chain of function calls. Extracting the function names
from these return addresses is what we call "symbolizing".

The extracted symbol is human-readable, to some extent. They are not the same as
what users see in the source code, because the function names are "mangled". For
example,
- `void foo(void)` becomes `_Z1foov`,
- `void foo(int, char)` becomes `_Z1fooic`, and
- `Foo::Bar()` becomes ` _ZN3Foo3BarEv`

according the
[mangling rule](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling)
set out in the Itanium C++ ABI.

### Demangling

Demangling is done by following the same mangling rule in the reverse direction.
In fact, one may demangle a symbol quite easily on a Linux or macOS, where GNU's
utility [c++filt](https://sourceware.org/binutils/docs/binutils/c_002b_002bfilt.html)
is usually present:
```bash
$ c++filt -n _ZN3Foo3BarEv
Foo::Bar()
```

> Also check out [llvm-cxxfilt](https://llvm.org/docs/CommandGuide/llvm-cxxfilt.html).

### Itanium C++ ABI

ABI, or [Application Binary Interface](https://en.wikipedia.org/wiki/Application_binary_interface),
dictates how binary objects should be interpreted and therefore communicated
with. Object files (and hence libraries comprising of them) compiled by two
different compilers for the same architecture can interoperate with each other
if they additionally agree on the ABI. For example, an ABI specifies the calling
convention, e.g. how are parameters and the return value should be pass around
between function calls, and how a C++ symbol should be mangled so that it is
uniquely identifiable in a program binary.

#### A bit history

In the 1990s, when Intel was developing its [Itanium](https://en.wikipedia.org/wiki/Itanium)
(formerly IA-64) architecture, it wanted its
[Intel C++ Compiler](https://en.wikipedia.org/wiki/Intel_C%2B%2B_Compiler)
to be able to interoperate with [GCC](https://gcc.gnu.org/), so it enlisted
engineers from Intel and GNU to design a uniform ABI for C++ compilers: the
[Itanium C++ ABI](https://itanium-cxx-abi.github.io).

The new ABI, though carrying the architecture name, is not tied to Itanium. GCC
contributors thought it was nice to have a uniform ABI for all architectures,
so they adhered to the ABI spec when implementing GCC compilers for others.
Moreover, because [Linux](https://en.wikipedia.org/wiki/Linux) were usually
compiled with GCC, the ABI became the lingua franca for all Linux programs for
the sake of interoperability with system libraries.

The story did not stop there. In the early days of [macOS](https://en.wikipedia.org/wiki/MacOS)
(formerly OS X), programs were compiled with GCC. Therefore, macOS's system
libraries use the Itanium C++ ABI. As a result, for interoperability, programs
on macOS adopted the ABI, too. Further, when Apple started contributing to
[LLVM](https://llvm.org/), a business-friendly, open-source compiler
infrastructure, it kept the Itanium C++ ABI as well to allow for a transparent
transition.

Therefore, the Itanium C++ ABI became the de-facto standard C++ ABI for programs
on both Linux and macOS, and is obeyed by compilers aiming for the two operating
systems.

■
