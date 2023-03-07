set(SOURCES
  "stb_impl.cpp"
)
add_library(stb_impl STATIC ${SOURCES})
target_include_directories(stb_impl SYSTEM PUBLIC
  "${PROJECT_SOURCE_DIR}/third_party/stb"
)
