add_library(MoonrakerPrinter STATIC
    include/Moonraker/MoonrakerPrinterAgent.hpp
    src/MoonrakerPrinterAgentImpl.hpp
    src/MoonrakerPrinterAgent.cpp
)
target_compile_features(MoonrakerPrinter PUBLIC cxx_std_17)
target_include_directories(MoonrakerPrinter PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(MoonrakerPrinter PUBLIC PrinterCore PRIVATE Qt6::Core Qt6::Network)

if(GPLATFORM_BUILD_TESTS)
    add_executable(MoonrakerPrinterAgentCheck tests/MoonrakerPrinterAgentCheck.cpp)
    target_compile_features(MoonrakerPrinterAgentCheck PRIVATE cxx_std_17)
    target_link_libraries(MoonrakerPrinterAgentCheck PRIVATE
        MoonrakerPrinter Qt6::Core Qt6::Network)
    add_test(NAME MoonrakerPrinterAgentCheck COMMAND MoonrakerPrinterAgentCheck)
    set_tests_properties(MoonrakerPrinterAgentCheck PROPERTIES TIMEOUT 30)
endif()
