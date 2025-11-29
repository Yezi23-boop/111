# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# 抑制第三方库的编译警告
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-attributes -Wno-unused-function")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-attributes -Wno-unused-function")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(111)

idf_component_get_property(esp_codec_dev_lib espressif__esp_codec_dev COMPONENT_LIB)
idf_component_get_property(driver_lib driver COMPONENT_LIB)
if(esp_codec_dev_lib AND driver_lib)
  target_link_libraries(${esp_codec_dev_lib} PRIVATE ${driver_lib})
  target_include_directories(${esp_codec_dev_lib} PRIVATE $ENV{IDF_PATH}/components/driver/i2c/include)
endif()

# 将WAV文件打包到SPIFFS分区(需要传入目录,不是单个文件)
#spiffs_create_partition_image(audio ${CMAKE_SOURCE_DIR}/audio_data FLASH_IN_PROJECT)
