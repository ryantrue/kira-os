idf_component_register(
    SRCS "app.cpp"
    INCLUDE_DIRS "include"
    REQUIRES kira_surface kira_platform
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
