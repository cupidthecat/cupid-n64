if(NOT EXISTS "${CAPTURE}" OR NOT EXISTS "${ROM}" OR NOT EXISTS "${PIF}")
    message(FATAL_ERROR "Capture test requires the executable, cartridge, and PIF inputs")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef suffix)
set(work "${WORK}/${suffix} 測試")
file(MAKE_DIRECTORY "${work}")
file(SHA256 "${ROM}" rom_before)
file(SHA256 "${PIF}" pif_before)

foreach(pass RANGE 1 2)
    execute_process(
        COMMAND "${CAPTURE}" "${work}/${pass}" 2 "${ROM}" --pif "${PIF}" --save none
                --max-instructions 100000000
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
    if(NOT result STREQUAL "0")
        message(FATAL_ERROR "Capture pass ${pass} failed (${result})\n${output}\n${errors}")
    endif()
endforeach()
foreach(name run.txt fields.csv audio.s16be last-field.ppm)
    file(SHA256 "${work}/1/${name}" first)
    file(SHA256 "${work}/2/${name}" second)
    if(NOT first STREQUAL second)
        message(FATAL_ERROR "Repeated captures differ: ${name}")
    endif()
endforeach()
file(READ "${work}/1/run.txt" metadata)
if(NOT metadata MATCHES "fields=2\n" OR NOT metadata MATCHES "capture_complete=1\n")
    message(FATAL_ERROR "Successful capture did not record both fields")
endif()
file(STRINGS "${work}/1/fields.csv" fields)
list(LENGTH fields field_lines)
if(NOT field_lines EQUAL 3)
    message(FATAL_ERROR "Capture must contain a header and two field records")
endif()
file(SIZE "${work}/1/last-field.ppm" image_size)
if(image_size LESS 100000)
    message(FATAL_ERROR "Capture did not contain a complete video field")
endif()

execute_process(
    COMMAND "${CAPTURE}" "${work}/short" 2 "${ROM}" --pif "${PIF}" --save none
            --max-instructions 1
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
if(NOT result STREQUAL "1")
    message(FATAL_ERROR "Incomplete capture must fail (${result})\n${output}\n${errors}")
endif()
file(READ "${work}/short/run.txt" metadata)
if(NOT metadata MATCHES "steps=1\n" OR NOT metadata MATCHES "capture_complete=0\n")
    message(FATAL_ERROR "Instruction-limited capture incorrectly reports completion")
endif()

file(SHA256 "${work}/1/run.txt" original)
execute_process(
    COMMAND "${CAPTURE}" "${work}/1" 2 "${ROM}" --pif "${PIF}" --save none
            --max-instructions 1
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors)
file(SHA256 "${work}/1/run.txt" after)
if(NOT result STREQUAL "1" OR NOT original STREQUAL after)
    message(FATAL_ERROR "Capture did not preserve an existing output directory")
endif()
file(SHA256 "${ROM}" rom_after)
file(SHA256 "${PIF}" pif_after)
if(NOT rom_before STREQUAL rom_after OR NOT pif_before STREQUAL pif_after)
    message(FATAL_ERROR "Capture modified an input image")
endif()
