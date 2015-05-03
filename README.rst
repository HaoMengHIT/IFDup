


Build
======

using clang to avoid some linking problem::

   mkdir build;cd build;
   export CC=clang
   export CXX=clang++
   cmake .. -DCMAKE_BUILD_TYPE=Debug
   cmake .. -DLLVM_RECOMMAND_VERSION=3.4
   make

Then, it will generate the `libIFDup.so` in `build/lib/` .

Note
======

You need to have the following installed in your system:
   - llvm-3.4
   - clang and clang++

You can also use `ccmake ..` in `build/` to change the compiling options.

Running
========

For example:

Compile the test.c to .bc file::

   clang -O0 -c -emit-llvm test.c -o test-O0.bc

Duplicate the instructions and only lock the duplicated instructions to prevent optimization, the original instructions are not changed::

   opt -load build/lib/libIFDup.so -InsDup test-O0.bc -o test-O0-insLock.bc 

O2 optimization::

   clang -O2 -c -emit-llvm test-O0-insLock.bc -o test-O2-insLock.bc

Unlock the locked instructions to restore the original instructions::

   opt -load build/lib/libIFDup.so -Unlock test-O2-insLock.bc -o test-O2-insUnlock.bc

Generate the executable files::

   clang -O0 test-O2-insUnlock.bc -o test-O2-InsUnlock







