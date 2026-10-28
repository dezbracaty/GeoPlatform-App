# The Qt desktop adapter is separate so BaseUI keeps its Core/Qml dependencies.
add_library(BaseUIDesktop STATIC
    include/QtTrayNotificationBackend.hpp
    src/QtTrayNotificationBackend.cpp)
target_include_directories(BaseUIDesktop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(BaseUIDesktop PUBLIC BaseUI Qt6::Gui PRIVATE Qt6::Widgets)
target_compile_features(BaseUIDesktop PUBLIC cxx_std_17)
set_target_properties(BaseUIDesktop PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON)
