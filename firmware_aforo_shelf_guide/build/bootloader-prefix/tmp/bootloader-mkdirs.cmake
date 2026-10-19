# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/dmrx/.espressif/v6.0.1/esp-idf/components/bootloader/subproject"
  "/home/dmrx/Documents/proyecto_selftiq26-2/firmware_ESP32-shelf_guide_SelfIQ-26-2/firmware_aforo_shelf_guide/build/bootloader"
  "/home/dmrx/Documents/proyecto_selftiq26-2/firmware_ESP32-shelf_guide_SelfIQ-26-2/firmware_aforo_shelf_guide/build/bootloader-prefix"
  "/home/dmrx/Documents/proyecto_selftiq26-2/firmware_ESP32-shelf_guide_SelfIQ-26-2/firmware_aforo_shelf_guide/build/bootloader-prefix/tmp"
  "/home/dmrx/Documents/proyecto_selftiq26-2/firmware_ESP32-shelf_guide_SelfIQ-26-2/firmware_aforo_shelf_guide/build/bootloader-prefix/src/bootloader-stamp"
  "/home/dmrx/Documents/proyecto_selftiq26-2/firmware_ESP32-shelf_guide_SelfIQ-26-2/firmware_aforo_shelf_guide/build/bootloader-prefix/src"
  "/home/dmrx/Documents/proyecto_selftiq26-2/firmware_ESP32-shelf_guide_SelfIQ-26-2/firmware_aforo_shelf_guide/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/dmrx/Documents/proyecto_selftiq26-2/firmware_ESP32-shelf_guide_SelfIQ-26-2/firmware_aforo_shelf_guide/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/dmrx/Documents/proyecto_selftiq26-2/firmware_ESP32-shelf_guide_SelfIQ-26-2/firmware_aforo_shelf_guide/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
