cmake_minimum_required(VERSION 3.10)
include(cmake/toolchain.cmake)

project(ch58x)

enable_language(C)
enable_language(CXX)
enable_language(ASM)

# Custom linker script
#set(TARGET_LD_SCRIPT ${CMAKE_SOURCE_DIR}/ld/Link.ld)
# Custom startup
#set(TARGET_STARTUP_ASM ${CMAKE_SOURCE_DIR}/startup/startup_CH583.S)

# Add OpenWCH SDK
add_subdirectory(openwch)

# Add project sources
add_executable(ch58x 
src/main.c
src/printf.c
src/u8g2_dataflash_cb.c
src/hid/simple_hid.c
src/w25qxx/w25qxx.c
src/parser/parser.c
src/variant/variant.c
src/data_process/data_process.c
src/text_format.c
src/u8g2_cross/u8g2_arc.c
src/u8g2_cross/u8g2_bitmap.c
src/u8g2_cross/u8g2_box.c
src/u8g2_cross/u8g2_buffer.c
src/u8g2_cross/u8g2_button.c
src/u8g2_cross/u8g2_circle.c
src/u8g2_cross/u8g2_cleardisplay.c
src/u8g2_cross/u8g2_d_memory.c
src/u8g2_cross/u8g2_d_setup.c
src/u8g2_cross/u8g2_font.c
src/u8g2_cross/u8g2_fonts.c
src/u8g2_cross/u8g2_hvline.c
src/u8g2_cross/u8g2_intersection.c
src/u8g2_cross/u8g2_kerning.c
src/u8g2_cross/u8g2_line.c
src/u8g2_cross/u8g2_ll_hvline.c
src/u8g2_cross/u8g2_polygon.c
src/u8g2_cross/u8g2_setup.c
src/u8g2_cross/u8g2_ext_font.c
src/u8g2_cross/u8log.c
src/u8g2_cross/u8log_u8g2.c
src/u8g2_cross/u8log_u8x8.c
src/u8g2_cross/u8x8_8x8.c
src/u8g2_cross/u8x8_byte.c
src/u8g2_cross/u8x8_cad.c
src/u8g2_cross/u8x8_capture.c
src/u8g2_cross/u8x8_d_ssd1306_128x32.c
src/u8g2_cross/u8x8_d_ssd1306_128x64_noname.c
src/u8g2_cross/u8x8_display.c
src/u8g2_cross/u8x8_fonts.c
src/u8g2_cross/u8x8_gpio.c
src/u8g2_cross/u8x8_setup.c
src/u8g2_cross/u8x8_string.c
src/u8g2_cross/u8x8_u16toa.c
src/u8g2_cross/u8x8_u8toa.c
src/u8g2_cross/port/ch582/ch58x_soft_I2C.c
src/u8g2_cross/port/ch582/ch58x_u8g2.c
)

# FatFS sources (enable with -DENABLE_FATFS=ON)
option(ENABLE_FATFS "Enable FatFS file system support" OFF)
if(ENABLE_FATFS)
    target_sources(ch58x PRIVATE
        src/fatfs/ff.c
        src/fatfs/ffsystem.c
        src/fatfs/ffunicode.c
        src/fatfs/diskio.c
    )
    target_compile_definitions(ch58x PRIVATE ENABLE_FATFS)
endif()

target_link_libraries(ch58x OpenWCH58x)
# Generate .bin and .hex
generate_binary_file(ch58x)
generate_hex_file(ch58x)
