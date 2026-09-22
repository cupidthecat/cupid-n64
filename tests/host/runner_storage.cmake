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
        TIMEOUT 30
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

function(check_save_variant label device size)
    set(image "${WORK}/儲存-${label}.bin")
    string(REPEAT "Z" ${size} contents)
    file(WRITE "${image}" "${contents}")
    file(SHA256 "${image}" expected_hash)
    foreach(pass RANGE 1 2)
        execute_process(
            COMMAND "${RUNNER}" "${rom_copy}" --pif "${pif_copy}" --save "${device}"
                    --save-file "${image}" --max-instructions 1 ${ARGN}
            TIMEOUT 30
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE errors)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "${label} restart pass ${pass} failed (${result})\n${output}\n${errors}")
        endif()
        file(SIZE "${image}" actual_size)
        file(SHA256 "${image}" actual_hash)
        if(NOT actual_size EQUAL size OR NOT actual_hash STREQUAL expected_hash)
            message(FATAL_ERROR "${label} restart pass ${pass} changed the supplied save image")
        endif()
    endforeach()
endfunction()

function(check_pak_capacity banks)
    math(EXPR port "((${banks} - 1) % 4) + 1")
    math(EXPR size "${banks} * 32768")
    set(image "${WORK}/控制器-${banks}.pak")
    string(REPEAT "P" ${size} contents)
    file(WRITE "${image}" "${contents}")
    file(SHA256 "${image}" expected_hash)
    foreach(pass RANGE 1 2)
        execute_process(
            COMMAND "${RUNNER}" "${rom_copy}" --pif "${pif_copy}" --save none
                    --controller "${port}:gamepad" --accessory "${port}:controller-pak"
                    --pak-banks "${port}:${banks}" --pak-file "${port}:${image}" --max-instructions 1
            TIMEOUT 30
            RESULT_VARIABLE result
            OUTPUT_VARIABLE output
            ERROR_VARIABLE errors)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "Controller Pak ${banks}-bank restart pass ${pass} failed (${result})\n${output}\n${errors}")
        endif()
        file(SIZE "${image}" actual_size)
        file(SHA256 "${image}" actual_hash)
        if(NOT actual_size EQUAL size OR NOT actual_hash STREQUAL expected_hash)
            message(FATAL_ERROR "Controller Pak ${banks}-bank restart pass ${pass} changed the supplied image")
        endif()
    endforeach()
    file(REMOVE "${image}")
endfunction()

check_save_variant(sram32 sram32 32768)
check_save_variant(sram96 sram96 98304)
check_save_variant(sram128 sram128 131072)
check_save_variant(eeprom4k eeprom4k 512)
check_save_variant(eeprom16k eeprom16k 2048)
foreach(chip IN ITEMS mx29l0000 mx29l0001 mx29l1100 mx29l1101a mx29l1101b mx29l1101c mn63f81mpn)
    check_save_variant("flash-${chip}" flash 131072 --flash-chip "${chip}")
endforeach()
foreach(banks RANGE 1 62)
    check_pak_capacity(${banks})
endforeach()
