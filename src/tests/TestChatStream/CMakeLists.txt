# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the Mumble
# source tree or at <https://www.mumble.info/LICENSE>.

find_pkg(Qt6 COMPONENTS Widgets Test REQUIRED)

add_executable(TestChatStream TestChatStream.cpp)

target_link_libraries(TestChatStream PRIVATE mumble_client_object_lib)
target_link_libraries(TestChatStream PRIVATE Qt6::Widgets Qt6::Test)

set_target_properties(TestChatStream PROPERTIES AUTOMOC ON)

add_test(
	NAME TestChatStream
	COMMAND $<TARGET_FILE:TestChatStream>
	# Specifying the working directory is necessary, to make sure the dependent DLLs are found (on Windows)
	WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set_tests_properties(TestChatStream PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
