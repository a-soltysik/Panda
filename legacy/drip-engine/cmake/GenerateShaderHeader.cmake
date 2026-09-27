cmake_minimum_required(VERSION 3.25)

get_filename_component(FILE_NAME "${SPIRV_FILE}" NAME)

string(REGEX REPLACE "\\.spv$" "" BASE_NAME "${FILE_NAME}")
string(REPLACE "." ";" NAME_PARTS "${BASE_NAME}")
list(GET NAME_PARTS 0 FIRST_PART)
string(TOLOWER "${FIRST_PART}" VAR_NAME)
list(REMOVE_AT NAME_PARTS 0)

foreach (PART IN LISTS NAME_PARTS)
    string(SUBSTRING "${PART}" 0 1 HEAD)
    string(TOUPPER "${HEAD}" HEAD)
    string(SUBSTRING "${PART}" 1 -1 TAIL)
    string(APPEND VAR_NAME "${HEAD}${TAIL}")
endforeach ()

file(READ "${SPIRV_FILE}" SPV_HEX HEX)
string(LENGTH "${SPV_HEX}" HEX_LEN)
math(EXPR WORDS_COUNT "${HEX_LEN} / 8")
math(EXPR LAST "${WORDS_COUNT} - 1")

set(WORDS "")
foreach (i RANGE ${LAST})
    math(EXPR OFFSET "${i} * 8")
    string(SUBSTRING "${SPV_HEX}" ${OFFSET} 8 WORD_HEX)
    string(SUBSTRING "${WORD_HEX}" 0 2 B0)
    string(SUBSTRING "${WORD_HEX}" 2 2 B1)
    string(SUBSTRING "${WORD_HEX}" 4 2 B2)
    string(SUBSTRING "${WORD_HEX}" 6 2 B3)
    list(APPEND WORDS "0x${B3}${B2}${B1}${B0}U")
endforeach ()

list(JOIN WORDS ", " WORDS_STR)

configure_file(${CONFIG_FILE} "${HEADER}" @ONLY)