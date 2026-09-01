/*
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
*/

#ifndef VTRACER_FFI_H
#define VTRACER_FFI_H

#ifdef __cplusplus
extern "C" {
#endif

/* Status codes reported through out_status. */
#define VTRACER_OK         0
#define VTRACER_PATH_LIMIT 1
#define VTRACER_ERROR      2

/*
 * Traces raw RGBA8 (unpremultiplied) pixels into an SVG document string.
 * Returns a newly allocated NUL-terminated string the caller must free
 * with vtracer_free_string, or NULL on failure. Status: VTRACER_OK,
 * VTRACER_PATH_LIMIT (too complex even after auto tightening, see
 * out_path_count) or VTRACER_ERROR (details written to err_buf, ASCII).
 */
char *vtracer_trace_rgba(int width, int height,
                         const unsigned char *rgba,
                         int mode,
                         int hierarchical,
                         int binary,
                         int filter_speckle,
                         int color_precision,
                         int layer_difference,
                         int corner_threshold,
                         int length_threshold,
                         int max_iterations,
                         int splice_threshold,
                         int path_precision,
                         int max_paths,
                         int *out_status,
                         int *out_path_count,
                         char *err_buf,
                         int err_buf_len);

/* Frees a string returned by vtracer_trace_rgba / vtracer_ffi_version. */
void vtracer_free_string(char *str);

/* Returns the wrapped library version, ASCII. Free with vtracer_free_string. */
char *vtracer_ffi_version(void);

#ifdef __cplusplus
}
#endif

#endif // VTRACER_FFI_H
