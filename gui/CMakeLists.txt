cmake_minimum_required(VERSION 3.9)

project(gui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# As long as I couldn't force Qt package finder to properly select Qt version to use,
# then let's qmake output controls particular Qt used.
if (NOT QT_QMAKE_EXECUTABLE)
    set(QT_QMAKE_EXECUTABLE qmake)
endif()
execute_process(
    COMMAND "${QT_QMAKE_EXECUTABLE}" -version
    RESULT_VARIABLE qmakeRes
    OUTPUT_VARIABLE qmakeOut
)
if (NOT qmakeRes STREQUAL 0 OR NOT qmakeOut MATCHES ".*Qt version ([0-9])+\..*")
    message(FATAL_ERROR "unable to run qmake utility for determining Qt version")
endif()
set(QT_VERSION_MAJOR ${CMAKE_MATCH_1})
message(STATUS "Determined Qt version ${QT_VERSION_MAJOR}")

find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets LinguistTools REQUIRED)

file(GLOB PROJECT_SOURCES *.h *.cpp *.ui)
set(TS_FILES gui_ru.ts)

qt_add_resources(PROJECT_SOURCES rc/app.qrc)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    # Resources can be added in a form of manually created qrc file (see a couple of lines above)
    # or as another target - automatically created qrc file, embedded into a destination target.
    # Unfortunatelly the second case isn't visible for QtCreator/Designer.
    #file(GLOB rcIconFiles ${CMAKE_CURRENT_SOURCE_DIR}/rc/icons/*)
    #qt_add_resources(gui icns PREFIX /icons BASE ${CMAKE_CURRENT_SOURCE_DIR}/rc/icons FILES ${rcIconFiles})

    # Define target properties for Android with Qt 6 as:
    #set_property(TARGET gui APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
    #                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
    #
    # For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    qt_add_executable(gui MANUAL_FINALIZATION ${PROJECT_SOURCES})
    qt_add_translations(gui TS_FILES ${TS_FILES})
else()
    set(qmDir "${CMAKE_CURRENT_BINARY_DIR}/qm")
    file(MAKE_DIRECTORY ${qmDir})

    set_source_files_properties(${TS_FILES} PROPERTIES OUTPUT_LOCATION ${qmDir})
    qt5_create_translation(qmFileList ${PROJECT_SOURCES} ${TS_FILES})

    set(qmResourcesFile "${qmDir}/app_tr.qrc")
    file(WRITE ${qmResourcesFile} "<!DOCTYPE RCC><RCC version=\"1.0\">\n  <qresource prefix=\"i18n\">\n")
    foreach(qmFile ${qmFileList})
        file(RELATIVE_PATH qmFilename ${qmDir} ${qmFile})
        file(APPEND ${qmResourcesFile} "    <file alias=\"${qmFilename}\">${qmFile}</file>\n")
    endforeach()
    file(APPEND ${qmResourcesFile} "  </qresource>\n</RCC>\n")

    if(ANDROID)
        add_library(gui SHARED ${PROJECT_SOURCES})
        # Define properties for Android with Qt 5 after find_package() calls as:
        #set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        qt5_add_resources(RESOURCE_SOURCES ${qmResourcesFile})
        add_executable(gui ${PROJECT_SOURCES} ${RESOURCE_SOURCES})
    endif()
endif()

target_link_libraries(gui PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)

set_target_properties(gui PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS gui
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(gui)
endif()

set(SANE_PP_STUB ON)
# set(SANE_PP_CANCEL_VIA_SIGNAL_SUPPORT ON)
add_subdirectory(../sane-pp sane-pp)

target_link_libraries(gui PRIVATE sane-pp-v1)
