cmake_minimum_required(VERSION 3.25)
project(PersonalGalleryNative VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ENABLE_SKYRIM_VR OFF CACHE BOOL "" FORCE)

add_subdirectory(external/CommonLibSSE)
include(external/CommonLibSSE/cmake/CommonLibSSE.cmake)

find_package(directxtex CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_commonlibsse_plugin(PersonalGalleryNative
	NAME "PersonalGalleryNative"
	AUTHOR "Personal Gallery"
	SOURCES
		src/main.cpp
		src/Apply.cpp
		src/Filters.cpp
		src/Pictures.cpp
		src/Extras.cpp
		src/Look.cpp
		src/Magic.cpp
		src/Place.cpp
		src/UI.cpp
		src/Video.cpp
		src/Player.cpp)

target_compile_definitions(PersonalGalleryNative PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(PersonalGalleryNative PRIVATE Microsoft::DirectXTex imgui::imgui d3d11 dxgi mfplat mfreadwrite mfuuid propsys xaudio2 windowscodecs gdi32)

# stand-alone check of the filter port (not shipped)
add_executable(FilterTest EXCLUDE_FROM_ALL test/filtertest.cpp src/Filters.cpp)
target_compile_definitions(FilterTest PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE)
target_link_libraries(FilterTest PRIVATE Microsoft::DirectXTex)

# stand-alone check of video/audio decoding (not shipped)
add_executable(VideoTest EXCLUDE_FROM_ALL test/videotest.cpp src/Video.cpp src/Filters.cpp)
target_compile_definitions(VideoTest PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE)
target_link_libraries(VideoTest PRIVATE Microsoft::DirectXTex mfplat mfreadwrite mfuuid propsys xaudio2 windowscodecs gdi32)

# the generated plugin-info source relies on a PCH providing the standard library and CommonLib
target_precompile_headers(PersonalGalleryNative PRIVATE src/PCH.h)
