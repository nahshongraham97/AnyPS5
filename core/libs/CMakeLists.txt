if(NOT WIN32)
    string(REPLACE "-static " "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ")
    string(STRIP "${CMAKE_EXE_LINKER_FLAGS}" CMAKE_EXE_LINKER_FLAGS)
endif()

add_executable(nid_patcher
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/Sha1.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/NidCompute.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/NidResolver.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/ExportExclusions.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/ElfNidPatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/PeNidPatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/BinaryPatcherFactory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/main.cpp
)

target_include_directories(nid_patcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/nid/include)
target_compile_features(nid_patcher PRIVATE cxx_std_20)

if(MINGW)
    target_link_options(nid_patcher PRIVATE -static -static-libgcc -static-libstdc++)
    add_compile_options(-fno-asynchronous-unwind-tables)
endif()

function(configure_windows_unwind target)
    if(MINGW)
        if(NOT EXISTS "${CMAKE_OBJCOPY}")
            message(FATAL_ERROR "Windows DWARF unwinding requires objcopy")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_OBJCOPY}" --rename-section .eh_frame=.ehfram "$<TARGET_FILE:${target}>"
                VERBATIM
        )
    endif()
endfunction()

set(LIBC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/prx/libc)
add_library(libc SHARED EXCLUDE_FROM_ALL)
set(LIBS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(${LIBC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/prx/libc)
target_include_directories(libc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
configure_windows_unwind(libc)

set(UNPATCHED_DIR ${CMAKE_CURRENT_BINARY_DIR}/libs/unpatched)
set(PATCHED_DIR ${CMAKE_CURRENT_BINARY_DIR}/libs)
set(IMPLIB_DIR ${PATCHED_DIR}/implib)

set_target_properties(libc PROPERTIES
        PREFIX ""
        SUFFIX .prx
        RUNTIME_OUTPUT_DIRECTORY ${UNPATCHED_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${UNPATCHED_DIR}
        ARCHIVE_OUTPUT_DIRECTORY ${IMPLIB_DIR}
)

set(LIBC_UNPATCHED ${UNPATCHED_DIR}/libc.prx)
set(LIBC_PATCHED ${PATCHED_DIR}/libc.prx)

add_custom_command(
        OUTPUT ${LIBC_PATCHED}
        COMMAND ${CMAKE_COMMAND} -E copy ${LIBC_UNPATCHED} ${LIBC_PATCHED}
        COMMAND $<TARGET_FILE:nid_patcher> libc ${LIBC_PATCHED}
        DEPENDS nid_patcher libc ${LIBC_UNPATCHED}
        COMMENT "Patching NIDs for libc"
)
add_custom_target(patched_libc DEPENDS ${LIBC_PATCHED})

file(GLOB LIBS_DIRS LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/prx/*)
list(REMOVE_ITEM LIBS_DIRS ${LIBC_DIR})

set(PATCHED_OUTPUTS ${LIBC_PATCHED})

foreach(LIB_DIR ${LIBS_DIRS})
    if(IS_DIRECTORY ${LIB_DIR})
        get_filename_component(LIB_NAME ${LIB_DIR} NAME)

        if(EXISTS ${LIB_DIR}/CMakeLists.txt)
            set(LIBS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
            add_subdirectory(${LIB_DIR} ${CMAKE_CURRENT_BINARY_DIR}/prx/${LIB_NAME})
        else()
            add_library(${LIB_NAME} SHARED EXCLUDE_FROM_ALL ${LIB_DIR}/Export.cpp)
            target_include_directories(${LIB_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
            configure_windows_unwind(${LIB_NAME})
        endif()

        if(LIB_NAME STREQUAL "libkernel")
            target_link_libraries(${LIB_NAME} PRIVATE libc)
        else()
            target_link_libraries(${LIB_NAME} PRIVATE libkernel libc)
        endif()
        if(WIN32 AND LIB_NAME STREQUAL "libSceNet")
            target_link_libraries(${LIB_NAME} PRIVATE ws2_32)
            target_compile_definitions(${LIB_NAME} PRIVATE _WIN32_WINNT=0x0602)
        elseif(WIN32 AND LIB_NAME STREQUAL "libSceNetCtl")
            target_link_libraries(${LIB_NAME} PRIVATE ws2_32 iphlpapi)
            target_compile_definitions(${LIB_NAME} PRIVATE _WIN32_WINNT=0x0602)
        endif()

        set_target_properties(${LIB_NAME} PROPERTIES
                PREFIX ""
                SUFFIX .prx
                RUNTIME_OUTPUT_DIRECTORY ${UNPATCHED_DIR}
                LIBRARY_OUTPUT_DIRECTORY ${UNPATCHED_DIR}
                ARCHIVE_OUTPUT_DIRECTORY ${IMPLIB_DIR}
        )

        set(UNPATCHED_FILE ${UNPATCHED_DIR}/${LIB_NAME}.prx)
        set(PATCHED_FILE ${PATCHED_DIR}/${LIB_NAME}.prx)

        add_custom_command(
                OUTPUT ${PATCHED_FILE}
                COMMAND ${CMAKE_COMMAND} -E copy ${UNPATCHED_FILE} ${PATCHED_FILE}
                COMMAND $<TARGET_FILE:nid_patcher> ${LIB_NAME} --preserve-exports "${LIBC_UNPATCHED}" "${PATCHED_FILE}"
                DEPENDS nid_patcher ${LIB_NAME} ${UNPATCHED_FILE} ${LIBC_UNPATCHED}
                COMMENT "Patching NIDs for ${LIB_NAME}"
        )
        add_custom_target(patched_${LIB_NAME} DEPENDS ${PATCHED_FILE})

        list(APPEND PATCHED_OUTPUTS ${PATCHED_FILE})
    endif()
endforeach()

set(LIBCOHTML_PATCHED ${PATCHED_DIR}/libcohtml.prx)
set(LIBCOHTML_PROSPERO_PATCHED ${PATCHED_DIR}/libcohtml.Prospero.prx)
add_custom_command(
        OUTPUT ${LIBCOHTML_PROSPERO_PATCHED}
        COMMAND ${CMAKE_COMMAND} -E copy "${LIBCOHTML_PATCHED}" "${LIBCOHTML_PROSPERO_PATCHED}"
        DEPENDS ${LIBCOHTML_PATCHED}
        VERBATIM
)
add_custom_target(libcohtml_prospero DEPENDS ${LIBCOHTML_PROSPERO_PATCHED})
list(APPEND PATCHED_OUTPUTS ${LIBCOHTML_PROSPERO_PATCHED})

add_custom_target(libs DEPENDS ${PATCHED_OUTPUTS})

if(BUILD_TESTING)
    add_executable(guest_system_command_tests tests/GuestSystemCommand.cpp)
    target_include_directories(guest_system_command_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_system_command_tests PRIVATE libc)
    configure_windows_unwind(guest_system_command_tests)
    add_test(NAME guest_system_command COMMAND guest_system_command_tests)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/guest_system_command_working_directory")
    set_tests_properties(guest_system_command PROPERTIES
        TIMEOUT 20
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/guest_system_command_working_directory")
    if(WIN32)
        set_tests_properties(guest_system_command PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_collation_tests tests/GuestCollation.cpp)
    target_include_directories(guest_collation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_collation_tests PRIVATE libc)
    configure_windows_unwind(guest_collation_tests)
    add_test(NAME guest_collation COMMAND guest_collation_tests)
    if(WIN32)
        set_tests_properties(guest_collation PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_allocated_formatting_tests tests/GuestAllocatedFormatting.cpp)
    target_include_directories(guest_allocated_formatting_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_allocated_formatting_tests PRIVATE libc)
    configure_windows_unwind(guest_allocated_formatting_tests)
    add_test(NAME guest_allocated_formatting COMMAND guest_allocated_formatting_tests)
    if(WIN32)
        set_tests_properties(guest_allocated_formatting PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_condition_variable_tests tests/GuestConditionVariable.cpp)
    target_include_directories(guest_condition_variable_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_condition_variable_tests PRIVATE libkernel libc)
    configure_windows_unwind(guest_condition_variable_tests)
    add_test(NAME guest_condition_variable COMMAND guest_condition_variable_tests)
    if(WIN32)
        set_tests_properties(guest_condition_variable PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libkernel>")
    endif()
    add_executable(guest_standard_imports_tests tests/GuestStandardImports.cpp)
    target_include_directories(guest_standard_imports_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_standard_imports_tests PRIVATE libSceNet libkernel libc)
    configure_windows_unwind(guest_standard_imports_tests)
    add_test(NAME guest_standard_imports COMMAND guest_standard_imports_tests)
    if(WIN32)
        set_tests_properties(guest_standard_imports PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libSceNet>")
    endif()
    add_executable(guest_scanning_tests tests/GuestScanning.cpp)
    target_include_directories(guest_scanning_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_scanning_tests PRIVATE libc)
    configure_windows_unwind(guest_scanning_tests)
    add_test(NAME guest_scanning COMMAND guest_scanning_tests)
    if(WIN32)
        set_tests_properties(guest_scanning PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_file_scanning_tests tests/GuestFileScanning.cpp)
    target_include_directories(guest_file_scanning_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_file_scanning_tests PRIVATE libc)
    configure_windows_unwind(guest_file_scanning_tests)
    add_test(NAME guest_file_scanning COMMAND guest_file_scanning_tests)
    if(WIN32)
        set_tests_properties(guest_file_scanning PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_wide_strings_tests tests/GuestWideStrings.cpp)
    target_include_directories(guest_wide_strings_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_wide_strings_tests PRIVATE libc)
    configure_windows_unwind(guest_wide_strings_tests)
    add_test(NAME guest_wide_strings COMMAND guest_wide_strings_tests)
    if(WIN32)
        set_tests_properties(guest_wide_strings PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_cancellation_settings_tests tests/GuestCancellationSettings.cpp)
    target_include_directories(guest_cancellation_settings_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_cancellation_settings_tests PRIVATE libkernel libc)
    configure_windows_unwind(guest_cancellation_settings_tests)
    add_test(NAME guest_cancellation_settings COMMAND guest_cancellation_settings_tests)
    if(WIN32)
        set_tests_properties(guest_cancellation_settings PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libkernel>")
    endif()
    add_executable(guest_signal_set_tests tests/GuestSignalSet.cpp)
    target_include_directories(guest_signal_set_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_signal_set_tests PRIVATE libkernel libc)
    configure_windows_unwind(guest_signal_set_tests)
    add_test(NAME guest_signal_set COMMAND guest_signal_set_tests)
    if(WIN32)
        set_tests_properties(guest_signal_set PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libkernel>")
    endif()
    add_executable(guest_thread_name_tests tests/GuestThreadName.cpp)
    target_include_directories(guest_thread_name_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_thread_name_tests PRIVATE libkernel libc)
    configure_windows_unwind(guest_thread_name_tests)
    add_test(NAME guest_thread_name COMMAND guest_thread_name_tests)
    if(WIN32)
        set_tests_properties(guest_thread_name PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libkernel>")
    endif()
    add_executable(guest_thread_specific_tests tests/GuestThreadSpecific.cpp)
    target_include_directories(guest_thread_specific_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_thread_specific_tests PRIVATE libkernel libc)
    configure_windows_unwind(guest_thread_specific_tests)
    add_test(NAME guest_thread_specific COMMAND guest_thread_specific_tests)
    if(WIN32)
        set_tests_properties(guest_thread_specific PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libkernel>")
    endif()
    add_executable(guest_kernel_model_tests tests/GuestKernelModel.cpp
            prx/libkernel/HostRuntime/GuestKernelModel.cpp)
    target_include_directories(guest_kernel_model_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(MINGW)
        # This target links no PRX, so -fno-asynchronous-unwind-tables leaves the
        # C++ exception runtime unresolved when the test uses try/catch.
        target_compile_options(guest_kernel_model_tests PRIVATE -fasynchronous-unwind-tables)
    endif()
    add_test(NAME guest_kernel_model COMMAND guest_kernel_model_tests)
    add_executable(guest_foreground_user_tests tests/GuestForegroundUser.cpp)
    target_include_directories(guest_foreground_user_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_foreground_user_tests PRIVATE libSceUserService libc)
    configure_windows_unwind(guest_foreground_user_tests)
    add_test(NAME guest_foreground_user COMMAND guest_foreground_user_tests)
    if(WIN32)
        set_tests_properties(guest_foreground_user PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_test_executable(savedata_memory_growth_tests tests/SaveDataMemoryGrowth.cpp)
    target_include_directories(savedata_memory_growth_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(savedata_memory_growth_tests PRIVATE libSceSaveData.native)
    configure_windows_unwind(savedata_memory_growth_tests)
    if(MINGW)
        target_compile_options(savedata_memory_growth_tests PRIVATE -fasynchronous-unwind-tables)
    endif()
    add_test(NAME savedata_memory_growth COMMAND savedata_memory_growth_tests)
    add_test(NAME savedata_memory_growth_read_failure COMMAND savedata_memory_growth_tests read-failure)
    set_tests_properties(savedata_memory_growth_read_failure PROPERTIES SKIP_RETURN_CODE 77)
    if(WIN32)
        set_tests_properties(savedata_memory_growth savedata_memory_growth_read_failure PROPERTIES
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:libSceSaveData.native>")
    endif()
    add_library(host_thread_local_test_library SHARED tests/HostThreadLocalLibrary.cpp)
    set_target_properties(host_thread_local_test_library PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${ANYPS5_TEST_OUTPUT_DIRECTORY}>")
    target_include_directories(host_thread_local_test_library PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test_executable(host_thread_local_tests tests/HostThreadLocal.cpp)
    target_link_libraries(host_thread_local_tests PRIVATE host_thread_local_test_library)
    if(MINGW)
        target_compile_options(host_thread_local_test_library PRIVATE -fasynchronous-unwind-tables)
        target_compile_options(host_thread_local_tests PRIVATE -fasynchronous-unwind-tables)
    endif()
    add_test(NAME host_thread_local COMMAND host_thread_local_tests)
    set_tests_properties(host_thread_local PROPERTIES TIMEOUT 15)

    add_test_executable(savedata_core_write_replacement_tests tests/SaveDataWriteReplacement.cpp)
    target_include_directories(savedata_core_write_replacement_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(savedata_core_write_replacement_tests PRIVATE libSceSaveData)
    configure_windows_unwind(savedata_core_write_replacement_tests)
    add_test(NAME savedata_core_write_replacement COMMAND savedata_core_write_replacement_tests)

    add_test_executable(savedata_native_write_replacement_tests tests/SaveDataWriteReplacement.cpp)
    target_include_directories(savedata_native_write_replacement_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(savedata_native_write_replacement_tests PRIVATE SAVEDATA_NATIVE_BACKEND)
    target_link_libraries(savedata_native_write_replacement_tests PRIVATE libSceSaveData.native)
    configure_windows_unwind(savedata_native_write_replacement_tests)
    add_test(NAME savedata_native_write_replacement COMMAND savedata_native_write_replacement_tests)

    add_test_executable(savedata_replace_file_failure_tests tests/SaveDataFileWriteFailure.cpp)
    target_include_directories(savedata_replace_file_failure_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    configure_windows_unwind(savedata_replace_file_failure_tests)
    add_test(NAME savedata_replace_file_failure COMMAND savedata_replace_file_failure_tests)

    if(MINGW)
        target_compile_options(savedata_core_write_replacement_tests PRIVATE -fasynchronous-unwind-tables)
        target_compile_options(savedata_native_write_replacement_tests PRIVATE -fasynchronous-unwind-tables)
        target_compile_options(savedata_replace_file_failure_tests PRIVATE -fasynchronous-unwind-tables)
    endif()
    if(WIN32)
        set_tests_properties(savedata_core_write_replacement PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libSceSaveData>")
        set_tests_properties(savedata_native_write_replacement PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libSceSaveData.native>")
    endif()
endif()

if(MINGW)
    foreach(runtimeLinkFlag IN ITEMS -static-libgcc -static-libstdc++ -static)
        string(REPLACE "${runtimeLinkFlag}" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    endforeach()
endif()
