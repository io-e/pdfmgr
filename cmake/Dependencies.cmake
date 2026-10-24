# MuPDF and QPDF.
#  - Windows (vcpkg): ports `libmupdf` (CMake package unofficial-libmupdf,
#    target unofficial::libmupdf::libmupdf, static only) and `qpdf` (qpdf::libqpdf).
#  - Debian/Ubuntu: pkg-config for qpdf; MuPDF via pkg-config when it ships
#    mupdf.pc (Debian 13), otherwise find_library. Static libmupdf.a (Ubuntu 24.04)
#    additionally pulls its third-party libraries through pkg-config.

# --- QPDF -------------------------------------------------------------------
add_library(pdfmgr_qpdf INTERFACE)
find_package(qpdf CONFIG QUIET)
if(TARGET qpdf::libqpdf)
  target_link_libraries(pdfmgr_qpdf INTERFACE qpdf::libqpdf)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(QPDF REQUIRED IMPORTED_TARGET libqpdf>=11.0)
  target_link_libraries(pdfmgr_qpdf INTERFACE PkgConfig::QPDF)
endif()
# qpdf 11 warns unless the PointerHolder transition level is chosen; 4 = no
# PointerHolder at all (we only use std::shared_ptr). Harmless on qpdf 12.
target_compile_definitions(pdfmgr_qpdf INTERFACE POINTERHOLDER_TRANSITION=4)

# --- MuPDF ------------------------------------------------------------------
add_library(pdfmgr_mupdf INTERFACE)
find_package(unofficial-libmupdf CONFIG QUIET)
if(TARGET unofficial::libmupdf::libmupdf)
  target_link_libraries(pdfmgr_mupdf INTERFACE unofficial::libmupdf::libmupdf)
else()
  find_package(PkgConfig REQUIRED)
  find_library(MUPDF_LIBRARY NAMES mupdf REQUIRED)
  if(NOT MUPDF_LIBRARY MATCHES "\\.a$")
    pkg_check_modules(MUPDF QUIET IMPORTED_TARGET mupdf)
  endif()
  if(TARGET PkgConfig::MUPDF)
    target_link_libraries(pdfmgr_mupdf INTERFACE PkgConfig::MUPDF)
  else()
    find_path(MUPDF_INCLUDE_DIR mupdf/fitz.h REQUIRED)
    target_include_directories(pdfmgr_mupdf INTERFACE ${MUPDF_INCLUDE_DIR})
    target_link_libraries(pdfmgr_mupdf INTERFACE ${MUPDF_LIBRARY})
    if(MUPDF_LIBRARY MATCHES "\\.a$")
      # Static MuPDF: link its dependencies explicitly (order matters for GNU ld).
      find_library(MUPDF_THIRD_LIBRARY NAMES mupdf-third)
      if(MUPDF_THIRD_LIBRARY)
        target_link_libraries(pdfmgr_mupdf INTERFACE ${MUPDF_THIRD_LIBRARY})
      endif()
      set(_pc_deps harfbuzz freetype2 libopenjp2 jbig2dec gumbo mujs libjpeg zlib)
      foreach(dep IN LISTS _pc_deps)
        string(MAKE_C_IDENTIFIER "MUPDF_DEP_${dep}" var)
        pkg_check_modules(${var} QUIET IMPORTED_TARGET ${dep})
        if(TARGET PkgConfig::${var})
          target_link_libraries(pdfmgr_mupdf INTERFACE PkgConfig::${var})
        else()
          # Some distros ship no .pc for jbig2dec/mujs/gumbo: plain library lookup.
          string(REGEX REPLACE "^lib|2$" "" libname "${dep}")
          if(dep STREQUAL "libopenjp2")
            set(libname openjp2)
          endif()
          find_library(${var}_LIB NAMES ${libname} ${dep})
          if(${var}_LIB)
            target_link_libraries(pdfmgr_mupdf INTERFACE ${${var}_LIB})
          else()
            message(WARNING "MuPDF is static but dependency '${dep}' was not found; linking may fail")
          endif()
        endif()
      endforeach()
      target_link_libraries(pdfmgr_mupdf INTERFACE m)
    endif()
  endif()
endif()
