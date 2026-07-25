if (enable-systemd-service)
	configure_file (${CMAKE_CURRENT_SOURCE_DIR}/cairo-dock4.service.in ${CMAKE_CURRENT_BINARY_DIR}/cairo-dock4.service)
	install (FILES ${CMAKE_CURRENT_BINARY_DIR}/cairo-dock4.service DESTINATION ${unitdir})
endif()
