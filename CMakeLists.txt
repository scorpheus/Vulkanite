cmake_minimum_required(VERSION 3.19)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)


# create project
project(Vulkanite CXX C)

include(GetPrerequisites)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
#set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
#set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
# Tell find_package() to first search using Config mode before falling back to Module mode (for conan)
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR})

option(ACTIVATE_FSR2 "FSR2" ON)
option(ACTIVATE_IMGUI "IMGUI" ON)

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
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /GS- /Zc:__cplusplus /Ot")

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

conan_cmake_configure(REQUIRES glfw/3.3.8 glm/cci.20220420 stb/cci.20210910 tinyobjloader/2.0.0-rc10 fmt/9.1.0 spdlog/1.11.0 nlohmann_json/3.11.2 tinygltf/2.5.0 mikktspace/cci.20200325 draco/1.5.5 ktx/4.0.0 libbasisu/1.15.0 imgui/1.89.4 implot/0.14
					GENERATORS cmake_find_package_multi
                    OPTIONS 
					spdlog:wchar_support=True)
					
foreach(TYPE Debug Release) #RelWithDebInfo)
	conan_cmake_autodetect(settings BUILD_TYPE ${TYPE})
	set(CONAN_RUNTIME MultiThreaded$<$<CONFIG:Debug>:Debug>$<$<CONFIG:RelWithDebInfo>:Debug>)
	set(CONAN_SETTINGS ${settings} compiler.runtime=${CONAN_RUNTIME})
	set(CONAN_SETTINGS ${settings} build_type=$<$<CONFIG:Debug>:Debug>$<$<CONFIG:RelWithDebInfo>:RelWithDebInfo>$<$<CONFIG:Release>:Release>)
	
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
if(ACTIVATE_IMGUI)
find_package(imgui REQUIRED)
endif()
find_package(implot REQUIRED)

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
if(ACTIVATE_IMGUI)
LIST(APPEND SOURCE_FILES extern/imgui/imgui_impl_glfw.cpp extern/imgui/imgui_impl_vulkan.cpp)
endif()
LIST(APPEND SOURCE_FILES extern/FSR2/ffx_fsr2.h)

# create Vulkanite project
add_executable(Vulkanite ${SOURCE_FILES} ${SHADERS_FILES} ${GLSL_SOURCES} ${GLSL_HEADERS})

target_link_libraries(Vulkanite PUBLIC glfw::glfw glm::glm stb::stb tinyobjloader::tinyobjloader fmt::fmt spdlog::spdlog nlohmann_json::nlohmann_json TinyGLTF::TinyGLTF mikktspace::mikktspace draco::draco KTX::KTX libbasisu::libbasisu)
if(ACTIVATE_IMGUI)
    target_link_libraries(Vulkanite PUBLIC imgui::imgui implot::implot)
endif()

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
	set_property(TARGET Vulkanite PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! USD
set(USD_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/extern/OpenUSD" CACHE PATH "Path to the USD build directory")

if (WIN32)
    # ".lib" on Windows
    set(USD_LIB_SUFFIX ${CMAKE_STATIC_LIBRARY_SUFFIX}
        CACHE STRING "Extension of USD libraries")
else ()
    # ".so" on Linux, ".dylib" on MacOS
    set(USD_LIB_SUFFIX ${CMAKE_SHARED_LIBRARY_SUFFIX}
        CACHE STRING "Extension of USD libraries")
endif ()

foreach(TYPE Release Debug)#RelWithDebInfo)

	set(CONFIG_TYPE ${TYPE})
	if(${TYPE} STREQUAL "RelWithDebInfo")
		set(CONFIG_TYPE "Release")
	endif()

	# look for usd project
	set(pxr_DIR "${USD_SDK_DIR}/build_${CONFIG_TYPE}")

#	find_package(Boost PATHS pxr_DIR) # headers only

	target_link_libraries(Vulkanite PUBLIC  
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/tbb_debug${USD_LIB_SUFFIX}> 
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/tbb${USD_LIB_SUFFIX}> 
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/usd_ms${USD_LIB_SUFFIX}>
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/Ptex${USD_LIB_SUFFIX}>
								# add materialX
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/MaterialXCore${USD_LIB_SUFFIX}>
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/MaterialXFormat${USD_LIB_SUFFIX}>
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/MaterialXGenGlsl${USD_LIB_SUFFIX}>
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/MaterialXGenMdl${USD_LIB_SUFFIX}>
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/MaterialXGenOsl${USD_LIB_SUFFIX}>
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/MaterialXGenShader${USD_LIB_SUFFIX}>
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/MaterialXRender${USD_LIB_SUFFIX}>
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/MaterialXRenderGlsl${USD_LIB_SUFFIX}>
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/MaterialXRenderHw${USD_LIB_SUFFIX}>
								$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/lib/MaterialXRenderOsl${USD_LIB_SUFFIX}>
								)

	target_include_directories(Vulkanite PUBLIC	$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/include> 
													$<$<CONFIG:${CONFIG_TYPE}>:${pxr_DIR}/include/boost-1_78>)
												#	$<$<CONFIG:${CONFIG_TYPE}>:${Boost_INCLUDE_DIRS}>)

	install(DIRECTORY "${pxr_DIR}/lib/"
 			CONFIGURATIONS ${CONFIG_TYPE}
 	 		DESTINATION Vulkanite COMPONENT Vulkanite
 			FILES_MATCHING PATTERN "*.dll")
	install(DIRECTORY "${pxr_DIR}/lib/usd"
 			CONFIGURATIONS ${CONFIG_TYPE}
 			DESTINATION Vulkanite COMPONENT Vulkanite)
	install(DIRECTORY "${pxr_DIR}/bin/"
 			CONFIGURATIONS ${CONFIG_TYPE}
 			DESTINATION Vulkanite COMPONENT Vulkanite
 			FILES_MATCHING PATTERN "*.dll")
	install(DIRECTORY "${pxr_DIR}/libraries/"
 			CONFIGURATIONS ${CONFIG_TYPE}
 			DESTINATION Vulkanite/libraries COMPONENT Vulkanite)
endforeach()

# usd importer install
add_custom_target(usd_copy ALL
	COMMAND ${CMAKE_COMMAND} -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DBUILD_TYPE=$<CONFIG> -P ${CMAKE_BINARY_DIR}/cmake_install.cmake
	DEPENDS Vulkanite
)
set_target_properties(usd_copy PROPERTIES FOLDER "CMakePredefinedTargets")


# !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!! Ressources include in exe
option(INCLUDE_MODEL "Mettre le model 3D dans l'exe" OFF)

include(cmake/CMakeRC.cmake)

set(ENVMAP textures/limpopo_golf_course_1k.hdr)
#set(MODEL_GLTF_PATH models/abeautifulgame_ktx.glb)

#set(MODEL_GLTF_PATH models/simple_texture_plane.usda)
set(MODEL_GLTF_PATH models/simple_texture_cube.usda)
#set(MODEL_GLTF_PATH models/Attic_NVIDIA/Attic_NVIDIA.usd)

#set(MODEL_GLTF_PATH models/BoxTextured.glb)
#set(MODEL_GLTF_PATH models/abeautifulgame_draco.glb)
#set(MODEL_GLTF_PATH models/DragonAttenuation.glb)
#set(MODEL_GLTF_PATH models/IridescentDishWithOlives.glb)
#set(MODEL_GLTF_PATH models/MosquitoInAmber.glb)

#file(GLOB model_RC "models/glTF-KTX-BasisU/*.*")
#set(MODEL_GLTF_PATH models/suzanne_metal.glb)
#set(MODEL_GLTF_PATH models/BoxTextured.glb)
#set(MODEL_GLTF_PATH models/sponza.glb)
#set(MODEL_GLTF_PATH models/TransmissionTest.glb)

set(LIST_RESOURCES 
		${ENVMAP}
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
if(INCLUDE_MODEL)	
	list(APPEND LIST_RESOURCES ${MODEL_GLTF_PATH})
endif()

cmrc_add_resource_library(gltf-resources NAMESPACE gltf_rc ${LIST_RESOURCES})

set_property(TARGET gltf-resources PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
target_link_libraries(Vulkanite PUBLIC gltf-resources)
target_compile_definitions(Vulkanite PUBLIC ENVMAP="${ENVMAP}" MODEL_GLTF_PATH="${MODEL_GLTF_PATH}")
target_compile_options(Vulkanite PRIVATE $<$<CONFIG:Release>:/Zi>)
set_target_properties(Vulkanite PROPERTIES LINK_FLAGS_RELEASE "/DEBUG")

# dlss
#target_link_directories(Vulkanite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/DLSS/lib/Windows_x86_64/x86_64)
#target_link_libraries(Vulkanite PUBLIC debug nvsdk_ngx_s_dbg optimized nvsdk_ngx_s)
#target_include_directories(Vulkanite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/DLSS/include)

# FSR2
if(ACTIVATE_FSR2)
	target_link_directories(Vulkanite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/FSR2)
	target_link_libraries(Vulkanite PUBLIC ffx_fsr2_api_vk_x64 ffx_fsr2_api_x64)
	target_include_directories(Vulkanite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/FSR2)
	target_compile_definitions(Vulkanite PUBLIC ACTIVATE_FSR2=1)
endif()

# # MaterialX
# set(target_list "" CACHE INTERNAL "target_list")
# function(add_library name)
#     message("Adding library ${name}")
#     set(target_list ${target_list} ${name} CACHE INTERNAL "target_list")
#     _add_library(${name} ${ARGN})
# endfunction()
# set(MATERIALX_BUILD_SHARED_LIBS OFF)
# set(MATERIALX_BUILD_TESTS OFF)
# set(MATERIALX_BUILD_GEN_OSL OFF)
# set(MATERIALX_BUILD_GEN_MDL OFF)
# set(MATERIALX_BUILD_GEN_MSL OFF)
# set(MATERIALX_PYTHON_LTO OFF)
# set(MATERIALX_INSTALL_PYTHON OFF)
# set(MATERIALX_TEST_RENDER OFF)
# set(MATERIALX_OSL_LEGACY_CLOSURES OFF)
# set(MATERIALX_BUILD_RENDER OFF)

# add_subdirectory("extern/MaterialX")

# #target_link_directories(Vulkanite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/MaterialX/lib)
# target_link_libraries(Vulkanite PUBLIC MaterialXCore MaterialXFormat MaterialXGenGlsl MaterialXGenShader)# MaterialXFormat MaterialXGenGlsl MaterialXGenMdl MaterialXGenMsl MaterialXGenOsl MaterialXGenShader MaterialXRender MaterialXRenderGlsl MaterialXRenderHw MaterialXRenderOsl)
# #target_include_directories(Vulkanite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/MaterialX/include)
# set_target_properties(${target_list} PROPERTIES FOLDER "MaterialX")

# set(MaterialX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern/MaterialX/lib/cmake/MaterialX)
# find_package(MaterialX)
# target_link_libraries(Vulkanite PUBLIC MaterialXCore MaterialXFormat MaterialXGenGlsl MaterialXGenShader)

# imgui backend
if(ACTIVATE_IMGUI)
	target_include_directories(Vulkanite PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/extern/imgui)
	target_compile_definitions(Vulkanite PUBLIC ACTIVATE_IMGUI=1)
endif()

# modify where cmake install directory
set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Cmake prefix" FORCE)

# install binary, runtime dependencies and data dependencies
install(TARGETS Vulkanite ARCHIVE DESTINATION Vulkanite RUNTIME DESTINATION Vulkanite LIBRARY DESTINATION Vulkanite COMPONENT Vulkanite)
#install(DIRECTORY extern/DLSS/lib/Windows_x86_64/rel/ DESTINATION Vulkanite COMPONENT Vulkanite)
#add_custom_command(TARGET Vulkanite POST_BUILD 
 #              COMMAND ${CMAKE_COMMAND} -E copy_if_different
#               "${CMAKE_CURRENT_SOURCE_DIR}/extern/DLSS/lib/Windows_x86_64/rel/nvngx_dlss.dll"
#                $<TARGET_FILE_DIR:Vulkanite>)

# if not include GLTF, copy near the exe
if(NOT INCLUDE_MODEL)
	get_filename_component(PARENT_DIR ${MODEL_GLTF_PATH} DIRECTORY)
	install(FILES ${MODEL_GLTF_PATH} DESTINATION "Vulkanite/${PARENT_DIR}" COMPONENT Vulkanite)
endif()

# add the spherical harmonics extractor project
add_subdirectory("SphericalHarmonicsExtractor")
target_link_libraries(spherical_harmonics_extractor PUBLIC glm::glm stb::stb fmt::fmt spdlog::spdlog nlohmann_json::nlohmann_json)

