cmake_minimum_required(VERSION 3.25)
project(qgbe LANGUAGES C VERSION 0.1)

find_package(SDL2 REQUIRED)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_EXTENSIONS False)

add_executable(qgbe
        src/main.c
        src/utils/fsize.c
        src/emu.c
        src/bus.c
        src/cpu.c
        src/timer.c
        src/inst.c
        src/dis.c
        src/cpu_instrs.c
        src/ppu.c
        src/apu.c

        src/mbc/no_mbc.c
        src/mbc/mbc1.c
        src/mbc/mbc3.c)
target_link_libraries(qgbe PRIVATE ${SDL2_LIBRARIES})
target_include_directories(qgbe PRIVATE ${SDL2_INCLUDE_DIRS} src)

target_compile_definitions(qgbe PRIVATE QGBE_VERSION="${PROJECT_VERSION}")