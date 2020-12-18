# CMakeList.txt: проект CMake для CMakeProject3; включите исходный код и определения,
# укажите здесь логику для конкретного проекта.
#
cmake_minimum_required (VERSION 3.8)

# Добавьте источник в исполняемый файл этого проекта.
add_executable (CMakeProject3 "CMakeProject3.cpp" "CMakeProject3.h"  "CoalesedAllocator.h" "MemoryAllocator.h" "MemoryAllocator.cpp")

# TODO: Добавьте тесты и целевые объекты, если это необходимо.

add_subdirectory("rapidcheck-master")
target_link_libraries(CMakeProject3 rapidcheck)

target_compile_features(CMakeProject3 PRIVATE cxx_std_17)