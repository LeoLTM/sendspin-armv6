set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Compiler binaries are resolved via PATH, which the CI workflow sets to the
# abhiTronix Pi Zero W toolchain (cross-gcc-14.2.0-pi_0-1).  That toolchain
# ships an ARMv6-targeted sysroot (crt1.o, libc, libstdc++) so the binary will
# actually run on the Pi Zero W — unlike the Ubuntu apt toolchain, whose
# sysroot is compiled for ARMv7 and crashes at _start() on ARMv6 hardware.
set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# -marm:              Force ARM instruction set (avoid Thumb-1, which has no
#                     hard-float ABI support on ARMv6).
# -mcpu=arm1176jzf-s: Exact core in Pi Zero W / Pi 1; implies -march=armv6.
# -mfpu=vfp:          VFP co-processor present on arm1176jzf-s.
# -mfloat-abi=hard:   Pass float args in VFP registers (matches gnueabihf ABI).
set(CMAKE_C_FLAGS_INIT   "-marm -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-marm -mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
