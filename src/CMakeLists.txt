# Build TeXworks
# ==============

# Collect Source Files
# --------------------

# **NOTE:** _A `GLOB` operation is quick, but may not be appropriate. It may be
# better to maintain an explicit list of source files. Particularly, adding a
# new file to an explicit list will cause CMake to automatically reconfigure
# the next time `make` is invoked. When `GLOB` is used, the developer will have
# to remember to re-run `cmake` if a source file is added._
set(TEXWORKS_SRCS BibTeXFile.cpp
                  ResearchTemplates.cpp
                  ResearchWorkflow.cpp
                  ResearchProject.cpp
                  ResearchBibliography.cpp
                  ResearchVersionControl.cpp
  ResearchProjectOperations.cpp
                  ResearchEnvironment.cpp
                  ResearchAuthoring.cpp
                  ResearchTikz.cpp
                  ResearchSourceHistory.cpp
                  ResearchStart.cpp
                  ResearchArticle.cpp
                  ResearchSlides.cpp
                  ResearchSlideExport.cpp
                  ResearchAnnotations.cpp
                  ResearchFind.cpp
                  ResearchCommandCompletion.cpp
                  ResearchCompletion.cpp
                  ResearchCommands.cpp
                  ResearchMenuChoices.cpp
                  ResearchSettings.cpp
                  ResearchShorthand.cpp
                  ResearchAppearance.cpp
	ResearchMotion.cpp
                  ResearchOnboarding.cpp
                  ResearchMerge.cpp
                  ResearchBuild.cpp
                  ResearchPublication.cpp
                  ResearchBuildOutput.cpp
                  ResearchOutputPolicy.cpp
                  ResearchDependencies.cpp
                  ResearchCollaboration.cpp
                  ResearchReferences.cpp
                  ResearchWritingContext.cpp
                  CitationSelectDialog.cpp
                  CompletingEdit.cpp
                  Engine.cpp
                  FindDialog.cpp
                  HardWrapDialog.cpp
                  main.cpp
                  PDFDocumentWindow.cpp
                  PrefsDialog.cpp
                  ResourcesDialog.cpp
                  ScriptManagerWidget.cpp
                  Settings.cpp
                  TemplateDialog.cpp
                  TeXDocks.cpp
                  TeXDocumentWindow.cpp
                  TeXHighlighter.cpp
                  TWApp.cpp
                  TWScriptableWindow.cpp
                  TWScriptManager.cpp
                  TWSynchronizer.cpp
                  TWUtils.cpp
                  document/Document.cpp
                  document/SpellChecker.cpp
                  document/SpellCheckManager.cpp
                  document/TextDocument.cpp
                  document/TeXDocument.cpp
                  scripting/ECMAScriptInterface.cpp
                  scripting/ECMAScript.cpp
                  scripting/ScriptAPI.cpp
                  scripting/Script.cpp
                  scripting/ScriptObject.cpp
                  ui/ClickableLabel.cpp
                  ui/ClosableTabWidget.cpp
                  ui/ColorButton.cpp
                  ui/ConsoleWidget.cpp
                  ui/LineNumberWidget.cpp
                  ui/ListSelectDialog.cpp
                  ui/RemoveAuxFilesDialog.cpp
                  ui/ScreenCalibrationWidget.cpp
                  utils/CmdKeyFilter.cpp
                  utils/CommandlineParser.cpp
                  utils/FileVersionDatabase.cpp
                  utils/FullscreenManager.cpp
                  utils/ResourcesLibrary.cpp
                  utils/SystemCommand.cpp
                  utils/TextCodecs.cpp
                  utils/TypesetManager.cpp
                  utils/VersionInfo.cpp
                  utils/WindowManager.cpp
                  )

set(TEXWORKS_HDRS BibTeXFile.h
                  ResearchCollaboration.h
                  CitationSelectDialog.h
                  CompletingEdit.h
                  DefaultBinaryPaths.h
                  DefaultPrefs.h
                  Engine.h
                  FindDialog.h
                  GitRev.h
                  HardWrapDialog.h
                  PDFDocumentWindow.h
                  PrefsDialog.h
                  ResourcesDialog.h
                  ScriptManagerWidget.h
                  Settings.h
                  TemplateDialog.h
                  TeXDocks.h
                  TeXDocumentWindow.h
                  TeXHighlighter.h
                  TWApp.h
                  TWScriptableWindow.h
                  TWScriptManager.h
                  TWSynchronizer.h
                  TWUtils.h
                  TWVersion.h
                  InterProcessCommunicator.h
                  document/Document.h
                  document/SpellChecker.h
                  document/SpellCheckManager.h
                  document/TextDocument.h
                  document/TeXDocument.h
                  scripting/ScriptAPIInterface.h
                  scripting/ScriptLanguageInterface.h
                  scripting/ScriptAPI.h
                  scripting/Script.h
                  scripting/ScriptObject.h
                  ui/ClickableLabel.h
                  ui/ClosableTabWidget.h
                  ui/ColorButton.h
                  ui/ConsoleWidget.h
                  ui/LineNumberWidget.h
                  ui/ListSelectDialog.h
                  ui/RemoveAuxFilesDialog.h
                  ui/ScreenCalibrationWidget.h
                  utils/CmdKeyFilter.cpp
                  utils/CommandlineParser.h
                  utils/FileVersionDatabase.h
                  utils/FullscreenManager.h
                  utils/IniConfig.h
                  utils/ResourcesLibrary.h
                  utils/SystemCommand.h
                  utils/TextCodecs.h
                  utils/TypesetCommand.h
                  utils/TypesetManager.h
                  utils/VersionInfo.h
                  utils/WindowManager.h
                  )

if (WIN32)
  list(APPEND TEXWORKS_SRCS InterProcessCommunicatorWin.cpp)
elseif (APPLE)
  list(APPEND TEXWORKS_SRCS InterProcessCommunicatorDummy.cpp)
  list(APPEND TEXWORKS_SRCS utils/OSVersionInfo_mac.mm)
else ()
  list(APPEND TEXWORKS_SRCS InterProcessCommunicatorDBus.cpp)
endif ()

if (WITH_QTSCRIPT)
  list(APPEND TEXWORKS_SRCS scripting/JSScriptInterface.cpp scripting/JSScript.cpp)
  list(APPEND TEXWORKS_HDRS scripting/JSScriptInterface.h scripting/JSScript.h)
  add_definitions(-DWITH_QTSCRIPT)
endif (WITH_QTSCRIPT)

set(TEXWORKS_UIS CitationSelectDialog.ui
                 CompletingEdit.ui
                 Find.ui
                 HardWrapDialog.ui
                 PDFDocumentWindow.ui
                 PDFFind.ui
                 PrefsDialog.ui
                 Replace.ui
                 ResourcesDialog.ui
                 ScriptManagerWidget.ui
                 SearchResults.ui
                 TemplateDialog.ui
                 TeXDocumentWindow.ui
                 ToolConfig.ui
                 ui/ListSelectDialog.ui
                 )

set(TEXWORKS_RCS ${TeXworks_SOURCE_DIR}/res/resfiles.qrc
                 ${TeXworks_SOURCE_DIR}/res/resources.qrc)

FILE(GLOB TEXWORKS_TRANS_TS   "${TeXworks_SOURCE_DIR}/trans/*.ts")
FILE(GLOB QT_TRANS_TS CONFIGURE_DEPENDS "${TeXworks_SOURCE_DIR}/trans/qt/*.ts")

IF ( WIN32 )
  if (NOT MSVC)
    set(CREATE_MANIFEST TRUE)
    configure_file(${TeXworks_SOURCE_DIR}/res/TeXworks.exe.in.manifest TeXworks.exe.manifest)
  endif (NOT MSVC)
  configure_file(${TeXworks_SOURCE_DIR}/res/TeXworks.in.rc TeXworks.rc)
  # windres does not emit header dependencies for the version resource.
  set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/TeXworks.rc APPEND PROPERTY
    OBJECT_DEPENDS "${TeXworks_SOURCE_DIR}/src/TWVersion.h;${CMAKE_CURRENT_BINARY_DIR}/GitRev.h")
  list(APPEND TEXWORKS_SRCS ${CMAKE_CURRENT_BINARY_DIR}/TeXworks.rc)
ENDIF ( WIN32 )

# These source files along with the `.moc` files generated from some headers
# form the TeXworks scripting API. We export them to variables in the
# `PARENT_SCOPE` (top level CMake file) so that they will be available to the
# build processes of the scripting language plugins.
SET(TeXworks_SCRIPT_API
  ${CMAKE_CURRENT_SOURCE_DIR}/scripting/Script.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/scripting/ScriptAPI.cpp
  PARENT_SCOPE
)

SET(TeXworks_SCRIPT_API_H
  ${CMAKE_CURRENT_SOURCE_DIR}/scripting/Script.h
  ${CMAKE_CURRENT_SOURCE_DIR}/scripting/ScriptAPI.h
  ${CMAKE_CURRENT_SOURCE_DIR}/TWSystemCmd.h
  ${CMAKE_CURRENT_SOURCE_DIR}/Settings.h
  PARENT_SCOPE
)


# App Setup
# ---------

# On OS X, set GUI type to an `.app` bundle, gather icon files and add core system
# frameworks to the library link list.
IF ( APPLE )
  SET(GUI_TYPE MACOSX_BUNDLE)

  LIST(APPEND TeXworks_LIBS  "-framework Foundation")

  set(TeXworks_APP_ICONS "${TeXworks_SOURCE_DIR}/res/TeXworks.icns" "${TeXworks_SOURCE_DIR}/res/TeXworks-doc.icns")
  SET_SOURCE_FILES_PROPERTIES( ${TeXworks_APP_ICONS}
    PROPERTIES
      MACOSX_PACKAGE_LOCATION Resources
  )
ENDIF ()

# **WARNING:** _Windows is untested_
IF ( WIN32 )
  SET(GUI_TYPE WIN32)
ENDIF ()

# Compilation
# -----------

IF (NOT ${BUILD_SHARED_PLUGINS})
  IF (WITH_LUA AND LUA_FOUND)
    LIST(INSERT TeXworks_LIBS 0 TWLuaPlugin ${LUA_LIBRARIES})
  ENDIF()
  IF (WITH_PYTHON AND Python_Development_FOUND)
    LIST(INSERT TeXworks_LIBS 0 TWPythonPlugin ${Python_LIBRARIES})
  ENDIF()
ENDIF()

# Build the executable
ADD_EXECUTABLE(TeXworks ${GUI_TYPE}
  ${TEXWORKS_SRCS} ${TEXWORKS_HDRS}
  ${TEXWORKS_RCS}
  ${TeXworks_APP_ICONS}
)
add_dependencies(TeXworks GitRev)

if (UNIX AND NOT APPLE)
  set_target_properties(TeXworks PROPERTIES
    OUTPUT_NAME texworks
  )
endif ()

target_compile_options(TeXworks PRIVATE ${WARNING_OPTIONS})


# Add translations
target_add_qt_translations(TeXworks
  TS_FILES ${TEXWORKS_TRANS_TS} ${QT_TRANS_TS}
  INCLUDEPATH "../src/"
)

get_target_property(lupdatePath Qt${QT_VERSION_MAJOR}::lupdate LOCATION)
get_target_property(_sources TeXworks SOURCES)
add_custom_target(${PROJECT_NAME}_UpdateTranslations COMMAND "${CMAKE_COMMAND}"
  "-DOUTPUTFILE=\"${CMAKE_SOURCE_DIR}/trans/${PROJECT_NAME}_trans.pro\""
  "-DPROJECT_NAME=\"${PROJECT_NAME}\""
  "-DQt_LUPDATE_EXECUTABLE=\"${lupdatePath}\""
  "-DFILES=\"${_sources};${TEXWORKS_UIS};${TEXWORKS_TRANS_TS}\""
  "-DINCLUDEPATH=\"../src/\""
  -P "${PROJECT_SOURCE_DIR}/modules/QtPDF/CMake/Modules/UpdateTranslations.cmake"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
if (NOT TARGET UpdateTranslations)
  add_custom_target(UpdateTranslations)
endif (NOT TARGET UpdateTranslations)
add_dependencies(UpdateTranslations ${PROJECT_NAME}_UpdateTranslations)

IF (UNIX)
  # Required (at least) on OpenBSD to make scripting plugins work
  SET_TARGET_PROPERTIES(TeXworks PROPERTIES LINK_FLAGS -rdynamic)
ENDIF ()

# Specify libraries to be linked to TeXworks.
target_LINK_LIBRARIES(TeXworks QtPDF::qtpdf ${TeXworks_LIBS} )

# Disallow automatic casts from char* to QString (enforcing the use of tr() or
# explicitly specifying the string encoding)
target_compile_definitions(TeXworks PRIVATE QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_NO_CAST_FROM_BYTEARRAY)
if (NOT MSVC)
	# Set QT_STRICT_ITERATORS everywhere except for MSVC (QTBUG-78112)
	target_compile_definitions(TeXworks PRIVATE QT_STRICT_ITERATORS)
endif (NOT MSVC)

target_compile_definitions(TeXworks PRIVATE TW_BUILD_ID="${TW_BUILD_ID}")

# The old Qt lazy layout can lose unlaid paragraphs during a syntax update.
# Only omit the synchronous workaround with the verified local QtGui repair.
if (TARGET Qt6::Gui)
  get_target_property(twQtGuiArchive Qt6::Gui IMPORTED_LOCATION_RELEASE)
  if (EXISTS "${twQtGuiArchive}")
    file(SHA256 "${twQtGuiArchive}" twQtGuiHash)
    if (twQtGuiHash STREQUAL "d4a3ee2512fbe91d03d3b8d46900a8490f2f4ffb31bcd383fe897da3d2745299")
      get_filename_component(twQtGuiDir "${twQtGuiArchive}" DIRECTORY)
      get_filename_component(twQtTargetPrefix "${twQtGuiDir}/../.." ABSOLUTE)
      set(twHbArchive "${twQtTargetPrefix}/lib/libharfbuzz.a")
      if (NOT EXISTS "${twHbArchive}")
        message(FATAL_ERROR "Verified source shape cache requires pinned HarfBuzz 12.2.0")
      endif()
      file(SHA256 "${twHbArchive}" twHbHash)
      if (NOT twHbHash STREQUAL "07bbf496e89676dbad3cf40d84faf8ac87553f2299f1d1f6e2c51629976cac82")
        message(FATAL_ERROR "Source shape cache HarfBuzz archive identity mismatch")
      endif()
    endif()
    if (twQtGuiHash STREQUAL "89c7117fd080bd5761998392e1a33d294c16618ddf0be271dfc89f9057999d63")
      target_compile_definitions(TeXworks PRIVATE TW_QT_DISPLAY_LINE_SPACING)
    endif()
    # Neutral text and width-cache archives retain the verified document-layout
    # member byte-for-byte; keep their identities in both capability guards.
    if (twQtGuiHash STREQUAL "04e73aac179a2f1dacfc206cd7698fb9b29eeccd02c5e84c0a1b4c2e29c1a83a"
        OR twQtGuiHash STREQUAL "f8c846b6eb56123a4f7eb3b4be4e7c986b150650514fe117d195903a34af5690"
        OR twQtGuiHash STREQUAL "e64234376958e4a6e354276c47a9c530e37f0382bbed2890683c2bb7a8bf3d28"
        OR twQtGuiHash STREQUAL "d4a3ee2512fbe91d03d3b8d46900a8490f2f4ffb31bcd383fe897da3d2745299"
        OR twQtGuiHash STREQUAL "305e89466a1db119eba26633b3784d1f3690183c09c102a56bd0ac3f56ed7911"
        OR twQtGuiHash STREQUAL "da639aa199209f24342ae2a4ef52037b5eca877bac04c1599d1386e3fcd80dd3"
        OR twQtGuiHash STREQUAL "d663e0e33b94b18406e26aefbbed15b2e496649e9374df1cfbc8af5aee715005"
        OR twQtGuiHash STREQUAL "89c7117fd080bd5761998392e1a33d294c16618ddf0be271dfc89f9057999d63")
      target_compile_definitions(TeXworks PRIVATE TW_QT_SAFE_LAZY_LAYOUT)
    endif()
    if (twQtGuiHash STREQUAL "f8c846b6eb56123a4f7eb3b4be4e7c986b150650514fe117d195903a34af5690"
        OR twQtGuiHash STREQUAL "e64234376958e4a6e354276c47a9c530e37f0382bbed2890683c2bb7a8bf3d28"
        OR twQtGuiHash STREQUAL "d4a3ee2512fbe91d03d3b8d46900a8490f2f4ffb31bcd383fe897da3d2745299"
        OR twQtGuiHash STREQUAL "305e89466a1db119eba26633b3784d1f3690183c09c102a56bd0ac3f56ed7911"
        OR twQtGuiHash STREQUAL "da639aa199209f24342ae2a4ef52037b5eca877bac04c1599d1386e3fcd80dd3"
        OR twQtGuiHash STREQUAL "d663e0e33b94b18406e26aefbbed15b2e496649e9374df1cfbc8af5aee715005"
        OR twQtGuiHash STREQUAL "89c7117fd080bd5761998392e1a33d294c16618ddf0be271dfc89f9057999d63")
      target_compile_definitions(TeXworks PRIVATE TW_QT_LAZY_SOURCE_EDITS)
    endif()
  endif()
endif()

function (target_add_path_define target name path)
  if (IS_ABSOLUTE ${path})
    target_compile_definitions(${target} PRIVATE ${name}="${path}")
  else ()
    target_compile_definitions(${target} PRIVATE ${name}="${CMAKE_INSTALL_PREFIX}/${path}")
  endif ()
endfunction()

if (UNIX AND NOT APPLE)
  target_add_path_define(TeXworks "TW_HELPPATH" "${TeXworks_HELP_DIR}")
  target_add_path_define(TeXworks "TW_PLUGINPATH" "${TeXworks_PLUGIN_DIR}")
  target_add_path_define(TeXworks "TW_DICPATH" "${TeXworks_DIC_DIR}")
endif ()


# For OS X, generate `Info.plist` and add it to the `.app` bundle.
IF (APPLE)
  SET(VER ${TeXworks_VERSION})
  SET(REV ${TeXworks_GIT_HASH})
  CONFIGURE_FILE(${TeXworks_SOURCE_DIR}/res/TeXworks.in.plist
    TeXworks.plist
    @ONLY
  )

  SET_TARGET_PROPERTIES( TeXworks PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST
      ${CMAKE_CURRENT_BINARY_DIR}/TeXworks.plist
  )
ENDIF ()

IF (UNIX)
    SET_TARGET_PROPERTIES(TeXworks PROPERTIES COMPILE_FLAGS -fPIC)
ENDIF ()

# Installation
# ------------
if (WIN32)
  # On Windows, put everything into the root install directory
  INSTALL(TARGETS TeXworks
    RUNTIME DESTINATION . COMPONENT
  )
  # Poppler discovers its encoding data and base-font substitutions next to
  # the executable. These data files are needed for legacy, unembedded PDFs.
  INSTALL(DIRECTORY "${TeXworks_SOURCE_DIR}/res/research/pdf-support/poppler/"
    DESTINATION share/poppler)
  INSTALL(DIRECTORY "${TeXworks_SOURCE_DIR}/res/research/pdf-support/fonts/"
    DESTINATION share/fonts)
  INSTALL(FILES "${TeXworks_SOURCE_DIR}/res/research/pdf-support/README.md"
    DESTINATION share)
else ()
  INSTALL(TARGETS TeXworks
    RUNTIME DESTINATION bin COMPONENT
    BUNDLE DESTINATION . COMPONENT
  )
endif ()

IF ( UNIX AND NOT APPLE )
  INSTALL(FILES ${TeXworks_SOURCE_DIR}/res/images/TeXworks.png DESTINATION ${CMAKE_INSTALL_PREFIX}/${TeXworks_ICON_DIR}/512x512/apps)
  INSTALL(FILES ${TeXworks_SOURCE_DIR}/res/images/TeXworks-128.png DESTINATION ${CMAKE_INSTALL_PREFIX}/${TeXworks_ICON_DIR}/128x128/apps RENAME TeXworks.png)
  INSTALL(FILES ${TeXworks_SOURCE_DIR}/res/images/TeXworks-64.png DESTINATION ${CMAKE_INSTALL_PREFIX}/${TeXworks_ICON_DIR}/64x64/apps RENAME TeXworks.png)
  INSTALL(FILES ${TeXworks_SOURCE_DIR}/res/images/TeXworks-48.png DESTINATION ${CMAKE_INSTALL_PREFIX}/${TeXworks_ICON_DIR}/48x48/apps RENAME TeXworks.png)
  INSTALL(FILES
    ${TeXworks_SOURCE_DIR}/COPYING
    ${TeXworks_SOURCE_DIR}/README.md
    ${TeXworks_SOURCE_DIR}/NEWS
  DESTINATION ${CMAKE_INSTALL_PREFIX}/${TeXworks_DOCS_DIR})
  INSTALL(FILES ${TeXworks_SOURCE_DIR}/res/texworks.1 DESTINATION ${CMAKE_INSTALL_PREFIX}/${TeXworks_MAN_DIR})
  INSTALL(FILES ${TeXworks_SOURCE_DIR}/res/org.tug.texworks.desktop DESTINATION ${CMAKE_INSTALL_PREFIX}/${TeXworks_DESKTOP_DIR})
  INSTALL(FILES ${TeXworks_SOURCE_DIR}/res/org.tug.texworks.metainfo.xml DESTINATION ${CMAKE_INSTALL_PREFIX}/${TeXworks_APPDATA_DIR})
ENDIF ( UNIX AND NOT APPLE )

# On OS X, ensure there is always a `qt.conf` file as all hell will break loose
# if one isn't there.  This may need to be customized for use on non-Apple
# systems.
#
# Protip: If there are wierd errors like "Qt libraries loaded twice" or
# "looking for a tablet device", then something may have gone pear shaped with
# `qt.conf`.
IF ( APPLE )
  SET(QTCONF_DIR ${PROJECT_NAME}.app/Contents/Resources)
  SET(QTCONF_CONTENTS "[Paths]\nPlugins = PlugIns")

  INSTALL( CODE
    "
    FILE(WRITE \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/${QTCONF_DIR}/qt.conf\"
      \"${QTCONF_CONTENTS}\"
    )
    "
  )
ENDIF()
