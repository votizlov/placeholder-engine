# CMakeList.txt : CMake project for PlaceholderEngine, include source and define
# project specific logic here.
#

include("C:\\Users\\Admin\\source\\repos\\PlaceholderEngine\\donut\\compileshaders.cmake")
file(GLOB shaders "*.hlsl")
file(GLOB sources "*.cpp" "*.h")

set(project PlaceholderEngine)
set(folder "Build")
donut_compile_shaders_all_platforms(
    TARGET ${project}_shaders
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/shaders.cfg
    FOLDER ${folder}
    OUTPUT_BASE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${project}
)
# Add source to this project's executable.
add_executable (PlaceholderEngine "PlaceholderEngine.cpp" "PlaceholderEngine.h" "SceneEntity.cpp")
target_link_libraries(${project} donut_app donut_engine)
add_dependencies(${project} ${project}_shaders)
set_target_properties(${project} PROPERTIES FOLDER ${folder})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET PlaceholderEngine PROPERTY CXX_STANDARD 20)
endif()

if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3 /MP")
endif()

# TODO: Add tests and install targets if needed.
