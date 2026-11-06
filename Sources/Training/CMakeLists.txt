# The training course's server (Server.h), built as a static library against
# ZeroSpades' own ENet (Sources/ENet) and zlib. Gui/TrainingHost.cpp runs it on
# a thread of the game; nothing else ships with it.

file(GLOB TRAINING_SERVER_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
	"${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(training_server STATIC ${TRAINING_SERVER_SOURCES})

target_include_directories(training_server
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}"
	PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../ENet/include")
if(ZLIB_INCLUDE_DIR)
	target_include_directories(training_server PRIVATE "${ZLIB_INCLUDE_DIR}")
endif()
target_link_libraries(training_server PUBLIC ${ZLIB_LIBRARIES})

set_target_properties(training_server PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	FOLDER libs)

find_package(Threads REQUIRED)
target_link_libraries(training_server PUBLIC Threads::Threads)

if(WIN32)
	# windows.h's min/max macros, and MSVC's _s deprecations.
	target_compile_definitions(training_server PRIVATE
		_WIN32_WINNT=0x0601
		NOMINMAX
		WIN32_LEAN_AND_MEAN
		_CRT_SECURE_NO_WARNINGS)
	target_link_libraries(training_server PUBLIC ws2_32 winmm)
endif()

if(MSVC)
	target_compile_options(training_server PRIVATE /W3 /utf-8 /permissive- /Zc:__cplusplus /Zc:preprocessor)
endif()

# The server's headless test (Server/test/training_server_test.cpp): built on
# request, `cmake --build . --target training_server_test`, and run by CTest.
file(GLOB TRAINING_TEST_ENET "${CMAKE_CURRENT_SOURCE_DIR}/../ENet/*.c")
add_executable(training_server_test EXCLUDE_FROM_ALL
	"${CMAKE_CURRENT_SOURCE_DIR}/../../Server/test/training_server_test.cpp"
	${TRAINING_TEST_ENET})
target_include_directories(training_server_test PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/.."
	"${CMAKE_CURRENT_SOURCE_DIR}/../ENet/include")
target_link_libraries(training_server_test PRIVATE training_server)
set_target_properties(training_server_test PROPERTIES
	CXX_STANDARD 17
	CXX_STANDARD_REQUIRED ON
	FOLDER tests)
if(WIN32)
	target_compile_definitions(training_server_test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN
		_CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
endif()
add_test(NAME training_server COMMAND training_server_test)
