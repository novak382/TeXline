# File to build unit-tests

include_directories("${CMAKE_SOURCE_DIR}/src" ${TeXworks_INCLUDE_DIRS})

# Exercise the exact application's isolated workflow/recovery checks, without
# compiling or shipping another copy of the application. No TeX installation
# is required. The self-test redirects settings and history to a temporary dir.
add_test(NAME test_ResearchWorkflow COMMAND $<TARGET_FILE:TeXworks>
  --research-self-test "${CMAKE_CURRENT_BINARY_DIR}/research-workflow.json")
set_tests_properties(test_ResearchWorkflow PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
add_executable(test_ResearchDependencies ResearchDependencies_test.cpp
  "${CMAKE_SOURCE_DIR}/src/ResearchDependencies.cpp")
target_compile_options(test_ResearchDependencies PRIVATE ${WARNING_OPTIONS})
target_link_libraries(test_ResearchDependencies ${QT_LIBRARIES})
add_test(NAME test_ResearchDependencies COMMAND test_ResearchDependencies)

# BiBTeXFile
add_executable(test_BibTeXFile BibTeXFile_test.cpp BibTeXFile_test.h "${CMAKE_SOURCE_DIR}/src/BibTeXFile.cpp" "${CMAKE_SOURCE_DIR}/src/BibTeXFile.h")
target_compile_options(test_BibTeXFile PRIVATE ${WARNING_OPTIONS})
target_link_libraries(test_BibTeXFile ${QT_LIBRARIES} ${ZLIB_LIBRARIES} ${TEXWORKS_ADDITIONAL_LIBS})
add_test(NAME test_BibTeXFile COMMAND test_BibTeXFile WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/testcases")

# Scripting
add_executable(test_Scripting
	Scripting_test.cpp
	Scripting_test.h
	MockScriptingAPI.h
	"${CMAKE_SOURCE_DIR}/src/scripting/Script.cpp"
	"${CMAKE_SOURCE_DIR}/src/scripting/ScriptObject.cpp"
	"${CMAKE_SOURCE_DIR}/src/scripting/ECMAScriptInterface.cpp"
	"${CMAKE_SOURCE_DIR}/src/scripting/ECMAScript.cpp"
)
if (WITH_QTSCRIPT)
	target_sources(test_Scripting
		PRIVATE "${CMAKE_SOURCE_DIR}/src/scripting/JSScriptInterface.cpp"
		PRIVATE "${CMAKE_SOURCE_DIR}/src/scripting/JSScript.cpp"
	)
	target_compile_definitions(test_Scripting PRIVATE WITH_QTSCRIPT)
endif (WITH_QTSCRIPT)
target_compile_options(test_Scripting PRIVATE ${WARNING_OPTIONS})
target_link_libraries(test_Scripting ${QT_LIBRARIES} ${ZLIB_LIBRARIES} ${TEXWORKS_ADDITIONAL_LIBS})
add_test(NAME test_Scripting COMMAND test_Scripting WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/testcases")

IF ( LUA_FOUND AND WITH_LUA )
	add_executable(test_LuaScripting
		LuaScripting_test.cpp
		LuaScripting_test.h
		MockScriptingAPI.h
		"${CMAKE_SOURCE_DIR}/src/scripting/Script.cpp"
		"${CMAKE_SOURCE_DIR}/src/scripting/ScriptObject.cpp"
	)
	target_compile_options(test_LuaScripting PRIVATE ${WARNING_OPTIONS})
	IF (NOT ${BUILD_SHARED_PLUGINS})
		target_link_libraries(test_LuaScripting TWLuaPlugin)
	ENDIF()

	target_link_libraries(test_LuaScripting ${QT_LIBRARIES} ${ZLIB_LIBRARIES} ${TEXWORKS_ADDITIONAL_LIBS})
	add_test(NAME test_LuaScripting COMMAND test_LuaScripting WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/testcases")
ENDIF ()

add_executable(byte_echo_test byte_echo_test.cpp)
target_compile_options(byte_echo_test PRIVATE ${WARNING_OPTIONS})

# UI
add_executable(test_UI
	UI_test.cpp
	UI_test.h
	"${CMAKE_SOURCE_DIR}/src/ui/ClickableLabel.cpp"
	"${CMAKE_SOURCE_DIR}/src/ui/ClosableTabWidget.cpp"
	"${CMAKE_SOURCE_DIR}/src/ui/ColorButton.cpp"
	"${CMAKE_SOURCE_DIR}/src/ui/ConsoleWidget.cpp"
	"${CMAKE_SOURCE_DIR}/src/ui/LineNumberWidget.cpp"
	"${CMAKE_SOURCE_DIR}/src/ui/ScreenCalibrationWidget.cpp"
)
target_compile_options(test_UI PRIVATE ${WARNING_OPTIONS})
target_link_libraries(test_UI ${QT_LIBRARIES} ${ZLIB_LIBRARIES} ${TEXWORKS_ADDITIONAL_LIBS})
add_test(NAME test_UI COMMAND test_UI WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/testcases")

# Windows TeX Live startup and console buffering. Pass a report path and the
# latexmk.exe path to run locally; optional TeX arguments must be test copies.
add_executable(test_ResearchCompile ResearchCompile_test.cpp "${CMAKE_SOURCE_DIR}/src/ui/ConsoleWidget.cpp")
target_compile_options(test_ResearchCompile PRIVATE ${WARNING_OPTIONS})
target_link_libraries(test_ResearchCompile ${QT_LIBRARIES})

# Supply a PDF and JSON report path; validates PDF navigation and click mapping.
add_executable(test_ResearchPDF ResearchPDF_test.cpp)
target_include_directories(test_ResearchPDF PRIVATE ${CMAKE_SOURCE_DIR}/modules/QtPDF/src)
target_link_libraries(test_ResearchPDF PRIVATE QtPDF::qtpdf ${QT_LIBRARIES})

# Utils
add_executable(test_Utils
	Utils_test.cpp
	Utils_test.h
	"${CMAKE_SOURCE_DIR}/src/utils/CommandlineParser.cpp"
	"${CMAKE_SOURCE_DIR}/src/utils/FileVersionDatabase.cpp"
	"${CMAKE_SOURCE_DIR}/src/utils/FullscreenManager.cpp"
	"${CMAKE_SOURCE_DIR}/src/utils/ResourcesLibrary.cpp"
	"${CMAKE_SOURCE_DIR}/src/utils/SystemCommand.cpp"
	"${CMAKE_SOURCE_DIR}/src/utils/TextCodecs.cpp"
	"${CMAKE_SOURCE_DIR}/src/utils/TypesetManager.cpp"
	"${CMAKE_SOURCE_DIR}/src/utils/VersionInfo.cpp"
)

target_compile_options(test_Utils PRIVATE ${WARNING_OPTIONS})
target_link_libraries(test_Utils ${QT_LIBRARIES} ${ZLIB_LIBRARIES} ${TEXWORKS_ADDITIONAL_LIBS})
add_dependencies(test_Utils GitRev)
add_test(NAME test_Utils COMMAND test_Utils WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/testcases")

if (APPLE)
	target_sources(test_Utils PRIVATE "${CMAKE_SOURCE_DIR}/src/utils/OSVersionInfo_mac.mm")
	target_link_libraries(test_Utils "-framework Foundation")
endif (APPLE)

# Document
add_executable(test_Document
	Document_test.cpp
	Document_test.h
	"${CMAKE_SOURCE_DIR}/src/document/Document.cpp"
	"${CMAKE_SOURCE_DIR}/src/document/SpellChecker.cpp"
	"${CMAKE_SOURCE_DIR}/src/document/SpellCheckManager.cpp"
	"${CMAKE_SOURCE_DIR}/src/document/TeXDocument.cpp"
	"${CMAKE_SOURCE_DIR}/src/document/TeXDocument.h"
	"${CMAKE_SOURCE_DIR}/src/document/TextDocument.cpp"
	"${CMAKE_SOURCE_DIR}/src/TWSynchronizer.cpp"
	"${CMAKE_SOURCE_DIR}/src/TWSynchronizer.h"
	"${CMAKE_SOURCE_DIR}/src/TeXHighlighter.h"
)
target_compile_options(test_Document PRIVATE ${WARNING_OPTIONS})
get_target_property(twDocumentTestDefinitions TeXworks COMPILE_DEFINITIONS)
if ("TW_QT_LAZY_SOURCE_EDITS" IN_LIST twDocumentTestDefinitions)
	target_compile_definitions(test_Document PRIVATE TW_QT_LAZY_SOURCE_EDITS)
endif()
if (WITH_POPPLERQT)
	target_compile_definitions(test_Document PRIVATE WITH_POPPLERQT)
	if (POPPLER_HAS_RUNTIME_VERSION)
		target_compile_definitions(test_Document PRIVATE POPPLER_HAS_RUNTIME_VERSION)
	endif (POPPLER_HAS_RUNTIME_VERSION)
endif (WITH_POPPLERQT)
target_link_libraries(test_Document QtPDF::qtpdf SyncTeX::synctex Hunspell::hunspell ${QT_LIBRARIES} ${ZLIB_LIBRARIES} ${TEXWORKS_ADDITIONAL_LIBS})
add_test(NAME test_Document COMMAND test_Document WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/testcases")

