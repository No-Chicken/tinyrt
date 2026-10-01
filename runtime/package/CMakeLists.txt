idf_component_register(SRCS "package.c" "package_psa.c"
    INCLUDE_DIRS "../include" PRIV_INCLUDE_DIRS "." REQUIRES mbedtls)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
