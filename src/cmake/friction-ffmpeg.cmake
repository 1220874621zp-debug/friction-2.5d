#
# Friction - https://friction.graphics
#
# Copyright (c) Ole-André Rodlie and contributors
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# See 'README.md' for more information.
#

if(WIN32)
    set(FFMPEG_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/sdk/include)
    set(FFMPEG_LIBRARIES
        avformat
        avcodec
        avutil
        swscale
        swresample)
    set(FFMPEG_LIBRARIES_DIRS ${CMAKE_SOURCE_DIR}/sdk/bin)
else()
    # First attempt: standard pkg-config search for FFmpeg 4.x compatible libraries
    pkg_check_modules(AVCODEC_COMPAT QUIET libavcodec<59)
    if(NOT AVCODEC_COMPAT_FOUND)
        # Search for ffmpeg4.4 pkgconfig paths on distributions with FFmpeg 5+ as default (Arch, Fedora, etc.)
        set(_FFMPEG44_PATHS
            "/usr/lib/ffmpeg4.4/pkgconfig"
            "/usr/lib64/ffmpeg4.4/pkgconfig"
            "/usr/lib/x86_64-linux-gnu/ffmpeg4.4/pkgconfig"
            "/usr/local/lib/ffmpeg4.4/pkgconfig"
        )
        foreach(_path ${_FFMPEG44_PATHS})
            if(EXISTS "${_path}/libavcodec.pc")
                set(ENV{PKG_CONFIG_PATH} "${_path}:$ENV{PKG_CONFIG_PATH}")
                break()
            endif()
        endforeach()
    endif()

    pkg_check_modules(AVFORMAT REQUIRED libavformat)
    pkg_check_modules(AVCODEC REQUIRED libavcodec)
    pkg_check_modules(AVUTIL REQUIRED libavutil)
    pkg_check_modules(SWSCALE REQUIRED libswscale)
    pkg_check_modules(SWRESAMPLE REQUIRED libswresample)
    set(FFMPEG_INCLUDE_DIRS ${AVFORMAT_INCLUDE_DIRS})
    set(FFMPEG_LIBRARIES
        ${AVFORMAT_LIBRARIES}
        ${AVCODEC_LIBRARIES}
        ${AVUTIL_LIBRARIES}
        ${SWSCALE_LIBRARIES}
        ${SWRESAMPLE_LIBRARIES})
    set(FFMPEG_LIBRARIES_DIRS ${AVFORMAT_LIBRARY_DIRS})
endif()
