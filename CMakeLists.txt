cmake_minimum_required(VERSION 3.20)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)


# create project
project(Vulkanite CXX C)

include(GetPrerequisites)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR})

# conan
include(conan.cmake)

set(CONAN_DISABLE_CHECK_COMPILER ON)
add_compile_definitions(CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD})

# install special recipe
if(DEFINED CONAN_COMMAND)
    set(CONAN_CMD ${CONAN_COMMAND})
else()
    conan_check(REQUIRED)
endif()

if(MSVC)
	# Make Visual Studio report accurate C++ version
	# See: https://devblogs.microsoft.com/cppblog/msvc-now-correctly-reports-__cplusplus/
  # /Wall emits warnings about the C++ standard library
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /GS- /Zc:__cplusplus")

	add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
	add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")

  add_definitions(/MP)
else()
	# Ignore Visual Studio pragma regions
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} --coverage -Og")
endif(MSVC)

# debug to list every path cmake check for find_library, very useful
#set(CMAKE_FIND_DEBUG_MODE TRUE)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# GLFW
conan_cmake_configure(REQUIRES glfw/3.3.8 glm/cci.20220420 stb/cci.20210910 tinyobjloader/2.0.0-rc10 spdlog/1.11.0 nlohmann_json/3.11.2 tinygltf/2.5.0 mikktspace/cci.20200325
					GENERATORS cmake_find_package_multi)
					
foreach(TYPE Debug RelWithDebInfo Release)
	conan_cmake_autodetect(settings BUILD_TYPE ${TYPE})
	conan_cmake_install(PATH_OR_REFERENCE .
						BUILD missing
						REMOTE conancenter
						SETTINGS ${settings})
endforeach()
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(stb REQUIRED)
find_package(tinyobjloader REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(tinygltf REQUIRED)
find_package(mikktspace REQUIRED)

link_libraries(glfw::glfw glm::glm stb::stb tinyobjloader::tinyobjloader spdlog::spdlog nlohmann_json::nlohmann_json TinyGLTF::TinyGLTF mikktspace::mikktspace)


# create Vulkanite project
set(SOURCE_FILES main.cpp
				vertex_config.h
				loader.cpp
				loader.h
				loaderGltf.cpp
				loaderGltf.h
				loaderObj.cpp
				loaderObj.h
				texture.cpp
				texture.h
				camera.cpp
				camera.h
				core_utils.cpp
				core_utils.h
				computeMikkTSpace.cpp
				computeMikkTSpace.h)
add_executable(Vulkanite ${SOURCE_FILES})

find_package(Vulkan)
if(Vulkan_FOUND)
	target_link_libraries(Vulkanite PUBLIC ${Vulkan_LIBRARY})
	target_include_directories(Vulkanite PUBLIC ${Vulkan_INCLUDE_DIRS})
endif()

if(WIN32)
	SET_TARGET_PROPERTIES(Vulkanite PROPERTIES COMPILE_FLAGS_RELEASE "/GL")
	SET_TARGET_PROPERTIES(Vulkanite PROPERTIES LINK_FLAGS_RELEASE "/LTCG")
	add_compile_options(/permissive- /std:c++latest)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Vulkanite)
	set_target_properties(Vulkanite PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/Vulkanite")
endif()

# shaders
include(cmake/utilities.cmake)
#--------------------------------------------------------------------------------------------------
# GLSL to SPIR-V custom build
compile_glsl_directory(
	SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders" 
	DST "${CMAKE_CURRENT_SOURCE_DIR}/Vulkanite/spv" 
	VULKAN_TARGET "vulkan1.0"
	DEPENDENCY ${VULKAN_BUILD_DEPENDENCIES}
	)
target_sources(Vulkanite PUBLIC ${GLSL_SOURCES} ${GLSL_HEADERS})
source_group("Shader Sources"  FILES ${GLSL_SOURCES})
source_group("Shader Headers"  FILES ${GLSL_HEADERS})


# modify where cmake install directory
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Cmake prefix" FORCE)

# install binary, runtime dependencies and data dependencies
install(TARGETS Vulkanite ARCHIVE DESTINATION Vulkanite RUNTIME DESTINATION Vulkanite LIBRARY DESTINATION Vulkanite COMPONENT Vulkanite)

install(FILES ${SPV_OUTPUT} DESTINATION vulkanite/spv COMPONENT Vulkanite)
install(DIRECTORY textures/ DESTINATION Vulkanite/textures COMPONENT Vulkanite)
install(DIRECTORY models/ DESTINATION Vulkanite/models COMPONENT Vulkanite)
