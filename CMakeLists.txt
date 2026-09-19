cmake_minimum_required(VERSION 3.20)
project(Vulkan14Compute LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# Vulkan パッケージの検索
find_package(Vulkan REQUIRED)

# glslc コンパイラの検索
find_program(GLSLC_EXECUTABLE glslc HINTS ${Vulkan_GLSLC_EXECUTABLE} "$ENV{VULKAN_SDK}/Bin" REQUIRED)

message(STATUS "Vulkan Header: ${Vulkan_INCLUDE_DIRS}")
message(STATUS "Vulkan Library: ${Vulkan_LIBRARIES}")
message(STATUS "glslc: ${GLSLC_EXECUTABLE}")

set(SHADER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders/compute.comp")
set(SHADER_SPV "${CMAKE_CURRENT_BINARY_DIR}/compute.spv")

# シェーダーコンパイル用のカスタムコマンド
add_custom_command(
    OUTPUT "${SHADER_SPV}"
    COMMAND "${GLSLC_EXECUTABLE}" "${SHADER_SRC}" -o "${SHADER_SPV}"
    DEPENDS "${SHADER_SRC}"
    COMMENT "Compiling compute.comp to compute.spv with glslc"
    VERBATIM
)

add_custom_target(CompileShaders ALL DEPENDS "${SHADER_SPV}")

add_executable(Vulkan14Compute src/main.cpp)

target_include_directories(Vulkan14Compute PRIVATE ${Vulkan_INCLUDE_DIRS})
target_link_libraries(Vulkan14Compute PRIVATE Vulkan::Vulkan)
add_dependencies(Vulkan14Compute CompileShaders)

# ビルド後に実行ファイルと同じディレクトリにも compute.spv をコピー
add_custom_command(
    TARGET Vulkan14Compute POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SHADER_SPV}"
        "$<TARGET_FILE_DIR:Vulkan14Compute>/compute.spv"
    COMMENT "Copying compute.spv to executable output directory"
)
