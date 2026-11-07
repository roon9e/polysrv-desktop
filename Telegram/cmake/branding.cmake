# This file is part of PolySRV Desktop,
# a Telegram Desktop fork for the PolySRV multi-server client.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

# Reads the branding constants from Telegram/build/branding and exposes each of
# them as a CMake variable named Branding_<Key>.
#
# Telegram/build/branding is the single source of truth. The C++ side gets it
# through Telegram/SourceFiles/core/branding.h, which
# Telegram/build/apply_branding.py generates from the same file.
function(desktop_app_parse_branding file)
    file(STRINGS ${file} lines)
    foreach (line ${lines})
        if (line MATCHES "^([A-Za-z0-9_]+)[ \t]+(.+)$")
            set(Branding_${CMAKE_MATCH_1} "${CMAKE_MATCH_2}" PARENT_SCOPE)
        endif()
    endforeach()
endfunction()
