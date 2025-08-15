# main

add_executable(png_pixel_bleed)
target_include_directories(png_pixel_bleed PRIVATE
        .
)
target_sources(png_pixel_bleed PRIVATE
        main.cpp
        png-pixel-bleeding.manifest
)
target_link_libraries(png_pixel_bleed PRIVATE
        wil
        imgui
        imgui_impl_win32
        imgui_impl_dx11
        DirectXTK
        RuntimeObject.lib
)

add_custom_command(TARGET png_pixel_bleed POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:png_pixel_bleed> ${CMAKE_BINARY_DIR}/$<TARGET_FILE_NAME:png_pixel_bleed>
)
