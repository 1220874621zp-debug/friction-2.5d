//! C ABI wrapper around vtracer 0.6.5 for Friction's bitmap tracing.
//!
//! Exposes a single-shot trace call that converts raw RGBA8 pixels into an
//! SVG string. When the result exceeds `max_paths` the parameters are
//! tightened and the conversion retried (up to RETRY_ROUNDS times); if the
//! limit still cannot be met the call reports status 1 so the caller can
//! reject illustration/photo-type images with a clear message.

use std::ffi::{c_char, c_int, CString};
use std::panic::{catch_unwind, AssertUnwindSafe};

use vtracer::{convert, ColorImage, ColorMode, Config, Hierarchical};
use visioncortex::PathSimplifyMode;

const RETRY_ROUNDS: usize = 4;
const VTRACER_LIB_VERSION: &str = "0.6.5";

pub const VTRACER_OK: c_int = 0;
pub const VTRACER_PATH_LIMIT: c_int = 1;
pub const VTRACER_ERROR: c_int = 2;

fn build_config(mode: c_int, hierarchical: c_int, binary: c_int,
                filter_speckle: c_int, color_precision: c_int,
                layer_difference: c_int, corner_threshold: c_int,
                length_threshold: c_int, max_iterations: c_int,
                splice_threshold: c_int, path_precision: c_int) -> Config {
    Config {
        color_mode: if binary != 0 { ColorMode::Binary } else { ColorMode::Color },
        hierarchical: if hierarchical != 0 {
            Hierarchical::Cutout
        } else {
            Hierarchical::Stacked
        },
        mode: match mode {
            1 => PathSimplifyMode::Polygon,
            2 => PathSimplifyMode::Spline,
            _ => PathSimplifyMode::None,
        },
        filter_speckle: filter_speckle.clamp(0, 128) as usize,
        color_precision: color_precision.clamp(1, 8),
        layer_difference: layer_difference.clamp(0, 256),
        corner_threshold: corner_threshold,
        length_threshold: length_threshold.max(0) as f64,
        max_iterations: max_iterations.clamp(1, 64) as usize,
        splice_threshold: splice_threshold,
        path_precision: if path_precision < 0 {
            None
        } else {
            Some(path_precision.clamp(0, 8) as u32)
        },
    }
}

/// Each retry round merges small speckles harder, keeps fewer colors and
/// tolerates bigger layer differences, trading fidelity for path count.
fn tighten(cfg: &mut Config, round: usize) {
    let step = 1i32 << round;
    cfg.filter_speckle = (cfg.filter_speckle as i32 + 4 * step).clamp(0, 128) as usize;
    cfg.color_precision = (cfg.color_precision - 1).clamp(1, 8);
    cfg.layer_difference = (cfg.layer_difference + 8 * step).clamp(0, 256);
}

unsafe fn write_err(err_buf: *mut c_char, err_buf_len: c_int, msg: &str) {
    if err_buf.is_null() || err_buf_len <= 0 { return; }
    let len = err_buf_len as usize;
    let bytes = msg.as_bytes();
    let n = bytes.len().min(len - 1);
    let dst = std::slice::from_raw_parts_mut(err_buf as *mut u8, len);
    dst[..n].copy_from_slice(&bytes[..n]);
    dst[n] = 0;
}

/// Returns a newly allocated SVG string (caller frees with
/// vtracer_free_string), or null on failure. Status: 0 ok,
/// 1 path limit exceeded (illustration/photo-like image),
/// 2 internal error (details in err_buf, ASCII only).
#[no_mangle]
pub unsafe extern "C" fn vtracer_trace_rgba(
    width: c_int, height: c_int, rgba: *const u8,
    mode: c_int, hierarchical: c_int, binary: c_int,
    filter_speckle: c_int, color_precision: c_int, layer_difference: c_int,
    corner_threshold: c_int, length_threshold: c_int,
    max_iterations: c_int, splice_threshold: c_int, path_precision: c_int,
    max_paths: c_int,
    out_status: *mut c_int, out_path_count: *mut c_int,
    err_buf: *mut c_char, err_buf_len: c_int,
) -> *mut c_char {
    let result = catch_unwind(AssertUnwindSafe(|| {
        run(width, height, rgba, mode, hierarchical, binary,
            filter_speckle, color_precision, layer_difference,
            corner_threshold, length_threshold, max_iterations,
            splice_threshold, path_precision, max_paths,
            out_path_count, err_buf, err_buf_len)
    }));
    match result {
        Ok(Ok(svg)) => {
            if !out_status.is_null() { *out_status = VTRACER_OK; }
            match CString::new(svg) {
                Ok(cstr) => cstr.into_raw(),
                Err(_) => {
                    if !out_status.is_null() { *out_status = VTRACER_ERROR; }
                    write_err(err_buf, err_buf_len, "SVG output contains NUL byte");
                    std::ptr::null_mut()
                }
            }
        }
        Ok(Err(code)) => {
            if !out_status.is_null() { *out_status = code; }
            std::ptr::null_mut()
        }
        Err(_) => {
            if !out_status.is_null() { *out_status = VTRACER_ERROR; }
            write_err(err_buf, err_buf_len, "vtracer internal panic");
            std::ptr::null_mut()
        }
    }
}

fn run(width: c_int, height: c_int, rgba: *const u8,
       mode: c_int, hierarchical: c_int, binary: c_int,
       filter_speckle: c_int, color_precision: c_int, layer_difference: c_int,
       corner_threshold: c_int, length_threshold: c_int,
       max_iterations: c_int, splice_threshold: c_int, path_precision: c_int,
       max_paths: c_int,
       out_path_count: *mut c_int,
       err_buf: *mut c_char, err_buf_len: c_int)
       -> Result<String, c_int> {
    if width <= 0 || height <= 0 || rgba.is_null() {
        unsafe { write_err(err_buf, err_buf_len, "invalid image dimensions") };
        return Err(VTRACER_ERROR);
    }
    let w = width as usize;
    let h = height as usize;
    let len = match w.checked_mul(h).and_then(|n| n.checked_mul(4)) {
        Some(n) => n,
        None => {
            unsafe { write_err(err_buf, err_buf_len, "image too large") };
            return Err(VTRACER_ERROR);
        }
    };
    let pixels = unsafe { std::slice::from_raw_parts(rgba, len) }.to_vec();
    let img = ColorImage { pixels, width: w, height: h };

    let limit = if max_paths < 1 { 1 } else { max_paths as usize };
    let mut cfg = build_config(mode, hierarchical, binary, filter_speckle,
                               color_precision, layer_difference,
                               corner_threshold, length_threshold,
                               max_iterations, splice_threshold, path_precision);
    let mut best_count = usize::MAX;
    for round in 0..=RETRY_ROUNDS {
        if round > 0 { tighten(&mut cfg, round - 1); }
        let svg = match convert(img.clone(), cfg.clone()) {
            Ok(svg) => svg,
            Err(e) => {
                unsafe { write_err(err_buf, err_buf_len, &e) };
                return Err(VTRACER_ERROR);
            }
        };
        let count = svg.paths.len();
        if !out_path_count.is_null() { unsafe { *out_path_count = count as c_int; } }
        if count <= limit {
            return Ok(svg.to_string());
        }
        if count < best_count { best_count = count; }
    }
    if !out_path_count.is_null() {
        unsafe { *out_path_count = best_count as c_int; }
    }
    Err(VTRACER_PATH_LIMIT)
}

/// Frees a string returned by vtracer_trace_rgba / vtracer_ffi_version.
#[no_mangle]
pub unsafe extern "C" fn vtracer_free_string(s: *mut c_char) {
    if !s.is_null() {
        drop(CString::from_raw(s));
    }
}

/// Returns the wrapped library version, ASCII. Free with vtracer_free_string.
#[no_mangle]
pub unsafe extern "C" fn vtracer_ffi_version() -> *mut c_char {
    let ver = format!("vtracer {}", VTRACER_LIB_VERSION);
    match CString::new(ver) {
        Ok(cstr) => cstr.into_raw(),
        Err(_) => std::ptr::null_mut(),
    }
}
