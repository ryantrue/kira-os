idf_component_register(
    SRCS "settings_bridge.cpp"
    INCLUDE_DIRS "include"
    REQUIRES kira_platform
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
