# main

add_executable(png_pixel_bleed)
target_include_directories(png_pixel_bleed PRIVATE
        .
)
target_sources(png_pixel_bleed PRIVATE
        main.cpp
)
target_link_libraries(png_pixel_bleed PRIVATE
        imgui
        imgui_impl_win32
        imgui_impl_dx11
)
