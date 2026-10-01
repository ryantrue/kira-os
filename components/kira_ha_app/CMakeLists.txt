idf_component_register(SRCS "app.cpp" INCLUDE_DIRS "include" REQUIRES kira_platform esp-brookesia)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
