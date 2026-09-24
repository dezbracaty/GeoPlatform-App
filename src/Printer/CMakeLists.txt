add_library(PrinterCore INTERFACE)

target_compile_features(PrinterCore INTERFACE cxx_std_17)

target_include_directories(PrinterCore INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_subdirectory(Moonraker)

set(PRINTER_SOURCES
    include/Printer/PrinterDeviceModel.hpp
    include/Printer/PrinterQtFacade.hpp
    include/Printer/PrinterRegistration.hpp
    src/PrinterDeviceModel.cpp
    src/PrinterQtFacade.cpp
    src/PrinterRegistration.cpp
    src/FixturePrinterAgent.hpp
    src/FixturePrinterAgent.cpp
    src/Flashforge/FlashforgePrinterAgent.hpp
    src/Flashforge/FlashforgePrinterAgent.cpp
)

set(FLASHNETWORK_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/FlashNetwork")

add_library(Printer STATIC ${PRINTER_SOURCES})

target_include_directories(Printer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(Printer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Flashforge
    ${FLASHNETWORK_SDK_DIR}
)

target_link_libraries(Printer PUBLIC
    PrinterCore
    Qt6::Core
    Foundation::QmlRegistration
)
target_link_libraries(Printer PRIVATE MoonrakerPrinter miniz)

gplatform_register_module(
    TARGET Printer
    HEADER Printer/PrinterRegistration.hpp
    FUNCTION GPlatform::Printer::registerModule
)

if(GPLATFORM_BUILD_TESTS)
    add_executable(PrinterQtFacadeCheck
        tests/PrinterQtFacadeCheck.cpp
    )
    target_compile_features(PrinterQtFacadeCheck PRIVATE cxx_std_17)
    target_link_libraries(PrinterQtFacadeCheck PRIVATE Printer Qt6::Core)
    gplatform_link_registered_modules(
        PrinterQtFacadeCheck Printer)
    add_test(NAME PrinterQtFacadeCheck COMMAND PrinterQtFacadeCheck)
endif()

if(GPLATFORM_BUILD_TESTS AND GPLATFORM_CORE_SOURCE_DIR)
    # This focused harness includes the production implementation and injects
    # SDK function pointers. Do not link Printer again (duplicate symbols).
    add_executable(FlashforgePrinterAgentShutdownCheck
        ${GPLATFORM_CORE_SOURCE_DIR}/tests/FlashforgePrinterAgentShutdownCheck.cpp)
    target_compile_features(FlashforgePrinterAgentShutdownCheck PRIVATE cxx_std_17)
    target_include_directories(FlashforgePrinterAgentShutdownCheck PRIVATE
        include src/Flashforge
        "$<TARGET_PROPERTY:Foundation,INTERFACE_INCLUDE_DIRECTORIES>"
        "${FLASHNETWORK_SDK_DIR}")
    target_link_libraries(FlashforgePrinterAgentShutdownCheck PRIVATE
        Qt6::Core spdlog::spdlog miniz)
    add_test(NAME FlashforgePrinterAgentShutdownCheck COMMAND FlashforgePrinterAgentShutdownCheck)
    set_tests_properties(FlashforgePrinterAgentShutdownCheck PROPERTIES TIMEOUT 15)
    set_tests_properties(PrinterQtFacadeCheck PROPERTIES TIMEOUT 15)
endif()
