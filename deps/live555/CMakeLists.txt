set(LIVE ${CMAKE_CURRENT_SOURCE_DIR}/live)

# download the latest live555 source
if (NOT EXISTS ${LIVE})
    file (DOWNLOAD http://www.live555.com/liveMedia/public/live555-latest.tar.gz ${CMAKE_CURRENT_SOURCE_DIR}/live555.tar.gz )
    EXECUTE_PROCESS(COMMAND ${CMAKE_COMMAND} -E tar xvf ${CMAKE_CURRENT_SOURCE_DIR}/live555.tar.gz RESULT_VARIABLE unpack_result WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    if(NOT unpack_result STREQUAL "0")
        message(FATAL_ERROR "Fetching live555 failed!")
    endif()
endif() 

FILE(
    GLOB 
    LIVEHELPERSOURCE 
    ${LIVE}/groupsock/*.c* 
    ${LIVE}/liveMedia/*.c* 
    ${LIVE}/UsageEnvironment/*.c* 
    ${LIVE}/BasicUsageEnvironment/*.c*
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(liblive555helper)

target_sources(liblive555helper PRIVATE
    ${LIVEHELPERSOURCE}
)

target_include_directories(
    liblive555helper 
    PRIVATE 
    ${LIVE}/groupsock/include 
    ${LIVE}/liveMedia/include 
    ${LIVE}/UsageEnvironment/include 
    ${LIVE}/BasicUsageEnvironment/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include)


if (WIN32)
	target_compile_definitions(
    liblive555helper PRIVATE _CRT_SECURE_NO_WARNINGS=1 NO_GETIFADDRS=1 NO_STD_LIB=1)
	target_link_libraries (liblive555helper ws2_32)
elseif (APPLE)
	target_compile_definitions(
    liblive555helper
    PRIVATE BSD=1 SOCKLEN_T=socklen_t _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE=1 NO_STD_LIB=1 NEED_XLOCALE_H=1)
else ()
	target_compile_definitions(
    liblive555helper
    PRIVATE BSD=1 SOCKLEN_T=socklen_t _FILE_OFFSET_BITS=64 _LARGEFILE_SOURCE=1 NO_STD_LIB=1)
endif()

# not link openssl currently
target_compile_definitions(liblive555helper PUBLIC NO_OPENSSL=1)

# include it to obs-rtsp directory
set_target_properties_obs(liblive555helper PROPERTIES FOLDER plugins/obs-rtsp)
