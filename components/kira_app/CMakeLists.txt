idf_component_register(
    SRCS "app.cpp" "voice_session.cpp"
    INCLUDE_DIRS "include"
    REQUIRES kira_surface kira_platform kira_audio esp_timer heap log
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_23)
