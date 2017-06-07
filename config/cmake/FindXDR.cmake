# Try to find the XDR library and set some required variables
#
# Once run this will define:
#
# XDR_FOUND                    = system has XDR lib
#
# XDR_LIBRARIES            = full path to the libraries, if required
#

include(CheckIncludeFile)
include(CheckCSourceCompiles)
include(CheckFunctionExists)
include(CheckTypeSize)
include(CheckLibraryExists)

## First try to find the required header files (rpc/types.h, rpc/xdr.h)
if(CYGWIN)
    include_directories("/usr/include/tirpc")
endif()

CHECK_INCLUDE_FILE(rpc/types.h HAVE_RPC_TYPES_H)
CHECK_INCLUDE_FILE(rpc/xdr.h HAVE_RPC_XDR_H)

if(NOT HAVE_RPC_TYPES_H)
    message(STATUS "Cannot find RPC headers (rpc/types.h).")
endif()

#check for the XDR functions: their interface and the libraries they're hidden in.
if (HAVE_RPC_TYPES_H)
    ## Now let's see if we need an extra lib to compile it
    set(XDR_INT_FOUND)
    CHECK_FUNCTION_EXISTS(xdr_int XDR_INT_FOUND)
    if (NOT XDR_INT_FOUND)
        foreach(lib nsl rpc tirpc)
            ## Try to find the corresponding lib
            set(XDR_INT_LIBRARY)
            find_library(XDR_INT_LIBRARY ${lib})

            if (XDR_INT_LIBRARY)
                CHECK_LIBRARY_EXISTS(${XDR_INT_LIBRARY} xdr_int "" XDR_INT_SYMBOL_FOUND)
            endif()
            if (XDR_INT_SYMBOL_FOUND)
                set(XDR_LIBRARIES ${XDR_INT_LIBRARY})
                set(XDR_INT_FOUND TRUE)
                break()
            endif()
        endforeach()
    endif()

    if(NOT XDR_INT_FOUND)
        message(SEND_ERROR "Could not locate xdr symbols in libc or libnsl.")
    else()
        set(XDR_FOUND TRUE)
    endif()
endif()
