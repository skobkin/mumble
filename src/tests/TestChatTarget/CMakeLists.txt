# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the Mumble
# source tree or at <https://www.mumble.info/LICENSE>.

find_pkg(Qt6 COMPONENTS Core Test REQUIRED)

add_executable(TestChatTarget TestChatTarget.cpp)

target_link_libraries(TestChatTarget PRIVATE mumble_client_object_lib)
target_link_libraries(TestChatTarget PRIVATE Qt6::Core Qt6::Test)

set_target_properties(TestChatTarget PROPERTIES AUTOMOC ON)

add_test(
	NAME TestChatTarget
	COMMAND $<TARGET_FILE:TestChatTarget>
	# Specifying the working directory is necessary, to make sure the dependent DLLs are found (on Windows)
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)
