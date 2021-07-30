cmake_minimum_required(VERSION 3.4.1)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
file(GLOB sources
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp/*.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp/include/MNN/tools/cpp/revertMNNModel.cpp)
set(sample app)
add_library(${sample} SHARED ${sources})
target_link_libraries(${sample} log GLESv3 EGL)
set_target_properties(${sample} PROPERTIES LIBRARY_OUTPUT_NAME native)
target_include_directories(${sample} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp/
        ${CMAKE_CURRENT_LIST_DIR}/src/main/cpp/include/MNN
        ${CMAKE_CURRENT_LIST_DIR}/src/main/cpp/include/MNN/source
        ${CMAKE_CURRENT_LIST_DIR}/src/main/cpp/include/MNN/source/backend
        ${CMAKE_CURRENT_LIST_DIR}/src/main/cpp/include/MNN/source/core
        ${CMAKE_CURRENT_LIST_DIR}/src/main/cpp/include/MNN/source/cv
        ${CMAKE_CURRENT_LIST_DIR}/src/main/cpp/include/MNN/source/math
        ${CMAKE_CURRENT_LIST_DIR}/src/main/cpp/include/MNN/source/shape
        ${CMAKE_CURRENT_LIST_DIR}/src/main/cpp/include/MNN/tools
        ${CMAKE_CURRENT_LIST_DIR}/src/main/cpp/include/MNN/tools/cpp)
target_link_libraries(
        ${sample}
        jnigraphics
        ${log-lib})
# [OpenCV Import Library]
add_library(opencv SHARED IMPORTED)
set_property(TARGET opencv PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/libs/${ANDROID_ABI}/libopencv_java4.so)
target_link_libraries(${sample} opencv)
add_custom_command(TARGET ${sample} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/libs/${ANDROID_ABI}/libopencv_java4.so
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
# [MNN Import Library]
add_library(mnn SHARED IMPORTED)
set_property(TARGET mnn PROPERTY IMPORTED_LOCATION ${CMAKE_CURRENT_SOURCE_DIR}/libs/${ANDROID_ABI}/libMNN.so)
target_link_libraries(${sample} mnn)
add_custom_command(TARGET ${sample} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/libs/${ANDROID_ABI}/libMNN.so
        ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})