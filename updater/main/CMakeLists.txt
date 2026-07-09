idf_component_register(SRCS "updater_main.c"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash fatfs sdmmc spi_flash app_update)
