
set(DEVKITPRO $ENV{DEVKITPRO})

set(CMAKE_SYSTEM_NAME "Generic")
set(CMAKE_C_COMPILER "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-gcc")
set(CMAKE_CXX_COMPILER "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-g++")
set(CMAKE_AR "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-gcc-ar" CACHE STRING "")
set(CMAKE_RANLIB "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-gcc-ranlib" CACHE STRING "")

set(PKG_CONFIG "${DEVKITPRO}/devkitARM/bin/arm-none-eabi-pkg-config" CACHE STRING "")
set(CPPFLAGS "-D_3DS -I${DEVKITPRO}/libctru/include -I${DEVKITPRO}/picaGL/include -I${DEVKITPRO}/portlibs/3ds/include")
set(CMAKE_C_FLAGS "${CPPFLAGS} -g -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft -ffunction-sections -fno-short-enums -O2" CACHE STRING "C flags")
set(CMAKE_CXX_FLAGS "${CPPFLAGS} ${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -std=gnu++11" CACHE STRING "C++ flags")

set(CMAKE_FIND_ROOT_PATH ${DEVKITPRO}/devkitARM ${DEVKITPRO}/libctru ${DEVKITARM}/portlibs/3ds)

set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "Shared libs not available")

link_directories(${DEVKITPRO}/libcrtu/lib ${DEVKITPRO}/portlibs/3ds/lib)