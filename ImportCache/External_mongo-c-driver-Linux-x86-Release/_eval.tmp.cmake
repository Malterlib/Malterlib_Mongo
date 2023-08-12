set(res [[0]])
if(ENABLE_STATIC AND NOT ENABLE_STATIC STREQUAL "BUILD_ONLY")
set(res [[1]])
endif()