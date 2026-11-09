# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(MUMBLE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/mumble")

set(TESTRESTARTARGUMENTS_SOURCES
	TestRestartArguments.cpp

	"${MUMBLE_SOURCE_DIR}/RestartArguments.cpp"
	"${MUMBLE_SOURCE_DIR}/RestartArguments.h"
)

add_executable(TestRestartArguments ${TESTRESTARTARGUMENTS_SOURCES})

set_target_properties(TestRestartArguments PROPERTIES AUTOMOC ON)

target_include_directories(TestRestartArguments PRIVATE ${MUMBLE_SOURCE_DIR})

target_link_libraries(TestRestartArguments PRIVATE Qt6::Test)

add_test(NAME TestRestartArguments COMMAND $<TARGET_FILE:TestRestartArguments>)
