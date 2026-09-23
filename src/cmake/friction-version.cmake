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

set(PROJECT_VERSION_MAJOR 1)
set(PROJECT_VERSION_MINOR 5)
set(PROJECT_VERSION_PATCH 0)
set(PROJECT_VERSION_TWEAK 0)

# version.txt at the repo root is the single source of truth for releases
# (build.bat packages the copy CMake writes into the build dir); without this
# the hardcoded 1.5.0 above leaked into package names / About dialog while
# releases were tagged v1.6.x.
if(EXISTS "${CMAKE_SOURCE_DIR}/version.txt")
    file(READ "${CMAKE_SOURCE_DIR}/version.txt" FRICTION_VER_RAW)
    string(STRIP "${FRICTION_VER_RAW}" FRICTION_VER)
    string(REPLACE "." ";" FRICTION_VER_PARTS "${FRICTION_VER}")
    list(LENGTH FRICTION_VER_PARTS FRICTION_VER_N)
    if(FRICTION_VER_N GREATER_EQUAL 3)
        list(GET FRICTION_VER_PARTS 0 PROJECT_VERSION_MAJOR)
        list(GET FRICTION_VER_PARTS 1 PROJECT_VERSION_MINOR)
        list(GET FRICTION_VER_PARTS 2 PROJECT_VERSION_PATCH)
        if(FRICTION_VER_N GREATER 3)
            list(GET FRICTION_VER_PARTS 3 PROJECT_VERSION_TWEAK)
        else()
            set(PROJECT_VERSION_TWEAK 0)
        endif()
    endif()
endif()

if (PROJECT_VERSION_TWEAK GREATER 0)
    set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}.${PROJECT_VERSION_TWEAK})
else()
    set(PROJECT_VERSION ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH})
endif()

set(CUSTOM_BUILD "" CACHE STRING "Custom build")
if (NOT CUSTOM_BUILD STREQUAL "")
    add_definitions(-DCUSTOM_BUILD="${CUSTOM_BUILD}")
endif()
option(FRICTION_OFFICIAL_RELEASE "" OFF)
if (${FRICTION_OFFICIAL_RELEASE})
    add_definitions(-DPROJECT_OFFICIAL)
endif()
