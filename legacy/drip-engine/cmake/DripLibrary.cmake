function(drip_library NAME)
    target_link_libraries(${NAME} PRIVATE drip::options drip::warnings)
    set_target_properties(${NAME} PROPERTIES FOLDER "Libraries")
endfunction()