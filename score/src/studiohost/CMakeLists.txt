# Studio (Laurence Who Studio): one window that holds Score and Tape
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(StudioHost WIN32
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../playback/internal/studiolinkpeer.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../playback/internal/studiolinkpeer.h
)
target_include_directories(StudioHost PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../playback/internal ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(StudioHost PRIVATE Qt6::Widgets Qt6::Network)
qt_add_resources(StudioHost studiohost_fonts
    PREFIX "/fonts"
    BASE "${PROJECT_SOURCE_DIR}/muse/framework/ui/data"
    FILES
        "${PROJECT_SOURCE_DIR}/muse/framework/ui/data/Jost-Regular.ttf"
        "${PROJECT_SOURCE_DIR}/muse/framework/ui/data/JostStar-Light.ttf"
)
if (WIN32)
    set_target_properties(StudioHost PROPERTIES OUTPUT_NAME "Studio")
endif()
install(TARGETS StudioHost RUNTIME DESTINATION bin)
