set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER   arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)

# ARMv6 + VFP hard float — required for Pi Zero W (ARM1176JZF-S)
set(CMAKE_C_FLAGS_INIT   "-march=armv6 -mfpu=vfp -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_INIT "-march=armv6 -mfpu=vfp -mfloat-abi=hard")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
