if(MINGW)
    get_filename_component(MINGW_PATH ${CMAKE_CXX_COMPILER} PATH)
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS 
        ${MINGW_PATH}/libgcc_s_seh-1.dll
        ${MINGW_PATH}/libstdc++-6.dll
        ${MINGW_PATH}/libwinpthread-1.dll
    )
endif()