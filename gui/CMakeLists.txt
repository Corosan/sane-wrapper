cmake_minimum_required(VERSION 3.5)

project(gui VERSION 0.1 LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Widgets LinguistTools REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Widgets LinguistTools REQUIRED)

file(GLOB PROJECT_SOURCES *.h *.cpp *.ui)

set(TS_FILES
        gui_ru_RU.ts)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(gui MANUAL_FINALIZATION ${PROJECT_SOURCES})

# Define target properties for Android with Qt 6 as:
#    set_property(TARGET gui APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation

    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    set(qmDir "${CMAKE_CURRENT_BINARY_DIR}/qm")
    file(MAKE_DIRECTORY ${qmDir})

    set_source_files_properties(${TS_FILES} PROPERTIES OUTPUT_LOCATION ${qmDir})
    qt5_create_translation(qmFileList ${PROJECT_SOURCES} ${TS_FILES})

    set(qmResourcesFile "${qmDir}/app.qrc")
    file(WRITE ${qmResourcesFile} "<!DOCTYPE RCC><RCC version=\"1.0\">\n  <qresource prefix=\"i18n\">\n")

    foreach(qm_file ${qmFileList})
      file(RELATIVE_PATH qm_filename ${qmDir} ${qm_file})
      file(APPEND ${qmResourcesFile} "    <file alias=\"${qm_filename}\">${qm_file}</file>\n")
    endforeach(qm_file)

    file(APPEND ${qmResourcesFile} "  </qresource>\n</RCC>\n")

    if(ANDROID)
        add_library(gui SHARED ${PROJECT_SOURCES})
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
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

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(gui)
endif()

# set(SANE_PP_STUB ON)
add_subdirectory(../sane-pp sane-pp)

target_link_libraries(gui PRIVATE sane-pp-v1)
