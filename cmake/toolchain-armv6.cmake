set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# Target the exact ARM1176JZF-S core used in Pi Zero W / Pi 1.
# -mcpu=arm1176jzf-s implies the ARM instruction set (not Thumb-1).
# arm-linux-gnueabihf defaults to Thumb-1 for ARMv6, which does not
# support the hard-float ABI — using -mcpu avoids that error.
set(CMAKE_C_FLAGS_INIT   "-mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=arm1176jzf-s -mfpu=vfp -mfloat-abi=hard")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
