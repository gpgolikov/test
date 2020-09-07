list(APPEND ${PROJECT_NAME}_SOURCES
    multi_queue_processor.h
    consumers.h
    main.cpp)

add_executable(${PROJECT_NAME} ${${PROJECT_NAME}_SOURCES})
target_link_libraries(${PROJECT_NAME} CONAN_PKG::boost)

set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION bin)
