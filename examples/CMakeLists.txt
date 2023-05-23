set(CMD blend chamfer hollow edges egads2tri tire)

set(CMD_LIBS egads)
if (UNIX AND NOT APPLE)
	set(CMD_LIBS ${CMD_LIBS} rt)
endif()
foreach(cmd ${CMD})
    add_executable(${cmd} ${cmd}.c)
    target_link_libraries(${cmd} ${CMD_LIBS})
    set_target_properties(${cmd} PROPERTIES INSTALL_RPATH "\$ORIGIN/../lib/:\$ORIGIN/../lib/opencascade/")
endforeach()

