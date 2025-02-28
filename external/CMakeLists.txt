# imgui

add_library(imgui STATIC)
target_compile_definitions(imgui PUBLIC
        IMGUI_DISABLE_OBSOLETE_FUNCTIONS
        IMGUI_DISABLE_DEFAULT_FONT
        IMGUI_USE_WCHAR32
)
target_include_directories(imgui PUBLIC
        imgui
        imgui/misc/cpp
)
target_sources(imgui PRIVATE
        imgui/imconfig.h
        imgui/imstb_rectpack.h
        imgui/imstb_textedit.h
        imgui/imstb_truetype.h
        imgui/imgui.h
        imgui/imgui.cpp
        imgui/imgui_internal.h
        imgui/imgui_widgets.cpp
        imgui/imgui_tables.cpp
        imgui/imgui_draw.cpp
        imgui/imgui_demo.cpp
        imgui/misc/cpp/imgui_stdlib.h
        imgui/misc/cpp/imgui_stdlib.cpp
)
target_sources(imgui PUBLIC
        imgui/misc/debuggers/imgui.natvis
        imgui/misc/debuggers/imgui.natstepfilter
)

# imgui_impl_win32

add_library(imgui_impl_win32 STATIC)
target_include_directories(imgui_impl_win32 PUBLIC
        imgui/backends
)
target_sources(imgui_impl_win32 PRIVATE
        imgui/backends/imgui_impl_win32.h
        imgui/backends/imgui_impl_win32.cpp
)
target_link_libraries(imgui_impl_win32 PUBLIC imgui)

# imgui_impl_dx11

add_library(imgui_impl_dx11 STATIC)
target_include_directories(imgui_impl_dx11 PUBLIC
        imgui/backends
)
target_sources(imgui_impl_dx11 PRIVATE
        imgui/backends/imgui_impl_dx11.h
        imgui/backends/imgui_impl_dx11.cpp
)
target_link_libraries(imgui_impl_dx11 PUBLIC
        imgui
        dxgi.lib
        d3d11.lib
        d3dcompiler.lib
)
