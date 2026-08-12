# Copyright (c) 2026 Jason Gripp
# Licensed under the MIT License.

add_executable(kdepends)

target_sources(kdepends PRIVATE
	main.cpp
	mainwindow.cpp          mainwindow.h
	moduletab.cpp           moduletab.h
	filteredtable.cpp       filteredtable.h
	dependencytreemodel.cpp dependencytreemodel.h
	importsmodel.cpp        importsmodel.h
	exportsmodel.cpp        exportsmodel.h
	modulesmodel.cpp        modulesmodel.h
	icons.cpp               icons.h
	analysisengine.cpp      analysisengine.h
	importresolver.cpp      importresolver.h
	dependencyresolver.cpp  dependencyresolver.h
	pathresolver.cpp        pathresolver.h
	ldcache.cpp             ldcache.h
	elfparser.cpp           elfparser.h
	demangler.cpp           demangler.h
	binaryreader.cpp        binaryreader.h  binaryreader.inlines.h
	moduledata.h
	elfstructs.h
)

set_target_properties(kdepends PROPERTIES AUTOMOC ON)

# KXMLGUIClient resolves an .rc file from ":/kxmlgui5/<component>/<file>" before
# it falls back to the installed copy, so compiling it in makes an uninstalled
# run behave exactly like an installed one instead of silently dropping back to
# the built-in default layout.
qt_add_resources(kdepends "kdepends_xmlgui"
	PREFIX "/kxmlgui5/kdepends"
	FILES kdependsui.rc
)

# The application icon is compiled in so an uninstalled build-tree run shows
# the real icon; an installed run prefers the icon theme copy (see
# ApplicationIcon() in icons.cpp).
qt_add_resources(kdepends "kdepends_icon"
	PREFIX "/icons"
	BASE "icons"
	FILES icons/sc-apps-kdepends.svg
)

# KDECompilerSettings compiles with -fno-exceptions. The analysis tasks run on
# a thread pool and must not let an unexpected exception (a std::bad_alloc from
# a huge file, say) cross a thread boundary, so they wrap their work in
# try/catch and the target needs exceptions back.
kde_target_enable_exceptions(kdepends PRIVATE)

target_link_libraries(kdepends PRIVATE
	Qt6::Core
	Qt6::Gui
	Qt6::Widgets
	KF6::CoreAddons
	KF6::I18n
	KF6::XmlGui
	KF6::ConfigCore
	KF6::ConfigGui
	KF6::ConfigWidgets
	KF6::WidgetsAddons
	KF6::KIOGui
)

# CMAKE_BINARY_DIR is where ecm_setup_version() writes kdepends_version.h.
target_include_directories(kdepends PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_BINARY_DIR})

install(TARGETS kdepends ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(PROGRAMS kdepends.desktop DESTINATION ${KDE_INSTALL_APPDIR})
ecm_install_icons(ICONS icons/sc-apps-kdepends.svg DESTINATION ${KDE_INSTALL_ICONDIR})
