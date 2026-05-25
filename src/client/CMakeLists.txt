add_executable(DeadZoneClient
    main.cpp
    Game.cpp
    Renderer.cpp
    InputHandler.cpp
    AudioManager.cpp
    NetworkClient.cpp
    TextureCache.cpp
    FontManager.cpp
)

target_include_directories(DeadZoneClient
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_SOURCE_DIR}/src"
)

target_link_libraries(DeadZoneClient
    PRIVATE
        DeadZoneShared
        SDL2::SDL2
        SDL2::SDL2main
        SDL2_image::SDL2_image
        SDL2_mixer::SDL2_mixer
        SDL2_ttf::SDL2_ttf
)

if(WIN32)
    set_target_properties(DeadZoneClient PROPERTIES
        WIN32_EXECUTABLE TRUE
    )
endif()

target_compile_options(DeadZoneClient PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)
