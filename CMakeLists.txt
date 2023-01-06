cmake_minimum_required(VERSION 3.19)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)


# create project
project(Vulkanite CXX C)

include(GetPrerequisites)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")

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

conan_cmake_configure(REQUIRES glfw/3.3.8 glm/cci.20220420 stb/cci.20210910 tinyobjloader/2.0.0-rc10 fmt/9.1.0 spdlog/1.11.0 nlohmann_json/3.11.2 tinygltf/2.5.0 mikktspace/cci.20200325 draco/1.5.5 ktx/4.0.0 libbasisu/1.15.0
					GENERATORS cmake_find_package_multi
                    OPTIONS spdlog:wchar_support=True)
					
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
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(tinygltf REQUIRED)
find_package(mikktspace REQUIRED)
find_package(draco REQUIRED)
find_package(ktx REQUIRED)
find_package(libbasisu REQUIRED)


link_libraries(glfw::glfw glm::glm stb::stb tinyobjloader::tinyobjloader fmt::fmt spdlog::spdlog nlohmann_json::nlohmann_json TinyGLTF::TinyGLTF mikktspace::mikktspace draco::draco KTX::KTX libbasisu::libbasisu)

# shaders
include(cmake/utilities.cmake)
#--------------------------------------------------------------------------------------------------
# GLSL to SPIR-V custom build
compile_glsl_directory(
	SRC "${CMAKE_CURRENT_SOURCE_DIR}/shaders" 
	DST "${CMAKE_CURRENT_SOURCE_DIR}/spv" 
	VULKAN_TARGET "vulkan1.3"
	DEPENDENCY ${VULKAN_BUILD_DEPENDENCIES}
)

file(GLOB SOURCE_FILES "*.h" "*.cpp")

# create Vulkanite project
add_executable(Vulkanite ${SOURCE_FILES} ${SHADERS_FILES} ${GLSL_SOURCES} ${GLSL_HEADERS})

find_package(Vulkan)
if(Vulkan_FOUND)
	target_link_libraries(Vulkanite ${Vulkan_LIBRARY})
	target_include_directories(Vulkanite PUBLIC ${Vulkan_INCLUDE_DIRS})
endif()

if(WIN32)
	SET_TARGET_PROPERTIES(Vulkanite PROPERTIES COMPILE_FLAGS_RELEASE "/GL")
	SET_TARGET_PROPERTIES(Vulkanite PROPERTIES LINK_FLAGS_RELEASE "/LTCG")
	add_compile_options(/permissive- /std:c++latest)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Vulkanite)
	set_target_properties(Vulkanite PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/Vulkanite")
	set_property(TARGET Vulkanite PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()


# ressources include in exe
include(cmake/CMakeRC.cmake)

set(ENVMAP textures/limpopo_golf_course_1k.hdr)
#set(MODEL_GLTF_PATH models/abeautifulgame_ktx.glb)
set(MODEL_GLTF_PATH models/abeautifulgame_draco.glb)
#file(GLOB model_RC "models/glTF-KTX-BasisU/*.*")
#set(MODEL_GLTF_PATH models/suzanne_metal.glb)
#set(MODEL_GLTF_PATH models/BoxTextured.glb)
#set(MODEL_GLTF_PATH models/sponza.glb)
#set(MODEL_GLTF_PATH models/TransmissionTest.glb)

cmrc_add_resource_library(gltf-resources NAMESPACE gltf_rc 
	${ENVMAP}
	${MODEL_GLTF_PATH}	
	textures/WhiteTex.png
	spv/closesthit.rchit.spv
	spv/miss.rmiss.spv
	spv/raygen.rgen.spv
	spv/shader.frag.spv
	spv/shader.vert.spv
	spv/shadow.rmiss.spv
	spv/shaderMotionVector.frag.spv
	spv/shaderMotionVector.vert.spv	
)
set_property(TARGET gltf-resources PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_link_libraries(Vulkanite gltf-resources)
target_compile_definitions(Vulkanite PUBLIC ENVMAP="${ENVMAP}" MODEL_GLTF_PATH="${MODEL_GLTF_PATH}")

# dlss
target_link_directories(Vulkanite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/DLSS/lib/Windows_x86_64/x86_64)
target_link_libraries(Vulkanite debug nvsdk_ngx_s_dbg optimized nvsdk_ngx_s)
target_include_directories(Vulkanite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/DLSS/include)

# modify where cmake install directory
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Cmake prefix" FORCE)

# install binary, runtime dependencies and data dependencies
install(TARGETS Vulkanite ARCHIVE DESTINATION Vulkanite RUNTIME DESTINATION Vulkanite LIBRARY DESTINATION Vulkanite COMPONENT Vulkanite)
install(DIRECTORY extern/DLSS/lib/Windows_x86_64/rel/ DESTINATION Vulkanite COMPONENT Vulkanite)
add_custom_command(TARGET Vulkanite POST_BUILD 
               COMMAND ${CMAKE_COMMAND} -E copy_if_different
               "${CMAKE_CURRENT_SOURCE_DIR}/extern/DLSS/lib/Windows_x86_64/rel/nvngx_dlss.dll"              
                $<TARGET_FILE_DIR:Vulkanite>)

# add the spherical harmonics extractor project
add_subdirectory("SphericalHarmonicsExtractor")
