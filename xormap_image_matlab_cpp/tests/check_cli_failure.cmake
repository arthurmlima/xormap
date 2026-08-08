if(NOT DEFINED EXECUTABLE OR NOT DEFINED CLI_SUBCOMMAND OR
   NOT DEFINED CLI_OPTION OR NOT DEFINED CLI_VALUE OR NOT DEFINED EXPECTED)
    message(FATAL_ERROR "check_cli_failure.cmake received incomplete arguments")
endif()

execute_process(
    COMMAND "${EXECUTABLE}" "${CLI_SUBCOMMAND}" "${CLI_OPTION}" "${CLI_VALUE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE standard_output
    ERROR_VARIABLE standard_error
)

if(result EQUAL 0)
    message(FATAL_ERROR
        "CLI unexpectedly accepted ${CLI_SUBCOMMAND} ${CLI_OPTION} ${CLI_VALUE}")
endif()

set(combined_output "${standard_output}${standard_error}")
string(FIND "${combined_output}" "${EXPECTED}" expected_position)
if(expected_position EQUAL -1)
    message(FATAL_ERROR
        "CLI failure did not contain '${EXPECTED}'. Output: ${combined_output}")
endif()
