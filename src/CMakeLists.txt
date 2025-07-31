idf_component_register(SRCS "main.c"
                            "gvcp_handler.c"
                            "camera_handler.c" 
                            "wifi_manager.c"
                            "genicam_xml.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp32-camera)