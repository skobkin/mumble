# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(MUMBLE_SOURCE_DIR "${CMAKE_SOURCE_DIR}/src/mumble")

set(TESTUISTATESTORE_SOURCES
	TestUiStateStore.cpp

	"${MUMBLE_SOURCE_DIR}/UiStateStore.cpp"
	"${MUMBLE_SOURCE_DIR}/UiStateStore.h"
)

add_executable(TestUiStateStore ${TESTUISTATESTORE_SOURCES})

set_target_properties(TestUiStateStore PROPERTIES AUTOMOC ON)

target_include_directories(TestUiStateStore PRIVATE ${MUMBLE_SOURCE_DIR})

target_link_libraries(TestUiStateStore PRIVATE Qt6::Test)

add_test(NAME TestUiStateStore COMMAND $<TARGET_FILE:TestUiStateStore>)
