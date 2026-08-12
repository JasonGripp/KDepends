# Copyright (c) 2026 Jason Gripp
# Licensed under the MIT License.

cmake_minimum_required(VERSION 3.16)

project(kdepends VERSION 0.1 LANGUAGES CXX)

set(KF_MIN_VERSION "6.0.0")
set(QT_MIN_VERSION "6.5.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMSetupVersion)
include(ECMInstallIcons)
include(FeatureSummary)

# Generates kdepends_version.h from the project() version above, so the version
# number is defined in exactly one place; main.cpp feeds it to KAboutData.
ecm_setup_version(${PROJECT_VERSION}
	VARIABLE_PREFIX KDEPENDS
	VERSION_HEADER "${CMAKE_BINARY_DIR}/kdepends_version.h"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS CoreAddons I18n XmlGui Config ConfigWidgets WidgetsAddons KIO)

add_subdirectory(src)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
