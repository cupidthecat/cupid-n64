if(NOT EXISTS "${WRITER}")
    message(FATAL_ERROR "Concurrent storage test requires the writer helper")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(destination "${WORK}/shared.sra")
set(ready_one "${WORK}/one.ready")
set(ready_two "${WORK}/two.ready")
set(start "${WORK}/start")

execute_process(
    COMMAND "${WRITER}" write "${destination}" 17 "${ready_one}" "${start}" 16
    COMMAND "${WRITER}" write "${destination}" 34 "${ready_two}" "${start}" 16
    COMMAND "${CMAKE_COMMAND}"
        -DREADY_ONE=${ready_one}
        -DREADY_TWO=${ready_two}
        -DSTART=${start}
        -P ${CMAKE_CURRENT_LIST_DIR}/storage_gate.cmake
    RESULTS_VARIABLE results
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors)

foreach(result IN LISTS results)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Concurrent storage writer failed (${results})\n${output}\n${errors}")
    endif()
endforeach()

execute_process(COMMAND "${WRITER}" verify "${destination}" 17 34 RESULT_VARIABLE verify_result)
if(NOT verify_result EQUAL 0)
    message(FATAL_ERROR "Concurrent replacement produced a missing, wrong-sized, or torn storage image (${verify_result})")
endif()
