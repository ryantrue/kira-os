idf_component_register(
    SRCS "center.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES kira_platform kira_surface kira_audio kira_boot
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
