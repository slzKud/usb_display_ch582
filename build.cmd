@echo off
cd build
"C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\others\Build_Tools\Make\bin\make.exe" clean
cmake .. -G "Unix Makefiles" -DENABLE_FATFS="ON" -DTOOLCHAIN_PATH="C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC" -DCMAKE_C_COMPILER="C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC/bin/riscv-none-embed-gcc.exe" -DCMAKE_CXX_COMPILER="C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC/bin/riscv-none-embed-g++.exe" -DCMAKE_MAKE_PROGRAM="C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\others\Build_Tools\Make\bin\make.exe"
"C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\others\Build_Tools\Make\bin\make.exe"
cd ..