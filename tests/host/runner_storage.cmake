if(NOT EXISTS "${RUNNER}" OR NOT EXISTS "${ROM}" OR NOT EXISTS "${PIF}")
    message(FATAL_ERROR "Runner storage test requires the runner, cartridge, and PIF inputs")
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(rom_copy "${WORK}/測試 cartridge.z64")
set(pif_copy "${WORK}/啟動 firmware.rom")
set(save_file "${WORK}/儲存 data.sra")
file(COPY_FILE "${ROM}" "${rom_copy}")
file(COPY_FILE "${PIF}" "${pif_copy}")

foreach(pass RANGE 1 2)
    execute_process(
        COMMAND "${RUNNER}" "${rom_copy}" --pif "${pif_copy}" --save sram32 --save-file "${save_file}"
                --max-instructions 1
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE errors)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Unicode runner storage pass ${pass} failed (${result})\n${output}\n${errors}")
    endif()
    file(SIZE "${save_file}" save_size)
    if(NOT save_size EQUAL 32768)
        message(FATAL_ERROR "Runner wrote ${save_size} bytes instead of a 32 KiB SRAM image")
    endif()
endforeach()
