#!/usr/bin/env python3
import os
import subprocess
import concurrent.futures

CWD = "/home/lanrhyme/Projects/friction-2.5d"
HICOLOR_DIR = os.path.join(CWD, "src/app/icons/hicolor")
SCALABLE_DIR = os.path.join(HICOLOR_DIR, "scalable")

TABLER_SVGS = {
    # Actions
    "actions/copy.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8v-2a2 2 0 0 0 -2 -2h-8a2 2 0 0 0 -2 2v8a2 2 0 0 0 2 2h2"/></svg>''',
    "actions/cut.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="6" cy="7" r="3"/><circle cx="6" cy="17" r="3"/><path d="M8.6 8.6l10.4 10.4"/><path d="M8.6 15.4l10.4 -10.4"/></svg>''',
    "actions/paste.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="7" width="14" height="14" rx="2"/><path d="M9 5a2 2 0 0 1 2 -2h2a2 2 0 0 1 2 2v2h-6z"/></svg>''',
    "actions/document-new.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M14 3v4a1 1 0 0 0 1 1h4"/><path d="M17 21h-10a2 2 0 0 1 -2 -2v-14a2 2 0 0 1 2 -2h7l5 5v11a2 2 0 0 1 -2 2z"/><path d="M12 11v6"/><path d="M9 14h6"/></svg>''',
    "actions/document-open.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 4h4l3 3h7a2 2 0 0 1 2 2v8a2 2 0 0 1 -2 2h-14a2 2 0 0 1 -2 -2v-11a2 2 0 0 1 2 -2z"/></svg>''',
    "actions/document-save.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M6 4h10l4 4v10a2 2 0 0 1 -2 2h-12a2 2 0 0 1 -2 -2v-12a2 2 0 0 1 2 -2z"/><circle cx="12" cy="14" r="2"/><polyline points="14 4 14 8 8 8 8 4"/></svg>''',
    "actions/fullscreen.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 8v-4h4"/><path d="M4 16v4h4"/><path d="M16 4h4v4"/><path d="M16 20h4v-4"/></svg>''',
    "actions/group.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="7" height="7" rx="1"/><rect x="13" y="4" width="7" height="7" rx="1"/><rect x="4" y="13" width="7" height="7" rx="1"/><rect x="13" y="13" width="7" height="7" rx="1"/></svg>''',
    "actions/plugin.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 9a2 2 0 0 1 2 -2h10a2 2 0 0 1 2 2v6a2 2 0 0 1 -2 2h-10a2 2 0 0 1 -2 -2z"/><path d="M9 3v4"/><path d="M15 3v4"/><path d="M9 17v4"/><path d="M15 17v4"/></svg>''',
    "actions/select.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M6 3l12 11l-5.5 1l3.5 6l-2.5 1.5l-3.5 -6l-4 3.5z"/></svg>''',
    "actions/zoom_in.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="7"/><path d="M21 21l-6 -6"/><path d="M10 7v6"/><path d="M7 10h6"/></svg>''',
    "actions/zoom_out.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="10" cy="10" r="7"/><path d="M21 21l-6 -6"/><path d="M7 10h6"/></svg>''',
    "actions/zoom_all.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 8v-4h4"/><path d="M4 16v4h4"/><path d="M16 4h4v4"/><path d="M16 20h4v-4"/></svg>''',
    "actions/go-up.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 5l0 14"/><path d="M18 11l-6 -6"/><path d="M6 11l6 -6"/></svg>''',
    "actions/go-down.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 5l0 14"/><path d="M18 13l-6 6"/><path d="M6 13l6 6"/></svg>''',
    "actions/go-previous.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12l14 0"/><path d="M5 12l6 6"/><path d="M5 12l6 -6"/></svg>''',
    "actions/go-next.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12l14 0"/><path d="M13 18l6 -6"/><path d="M13 6l6 6"/></svg>''',

    # Legacy & Controls
    "legacy/play.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#ffffff" stroke="#ffffff" stroke-width="1"><path d="M7 4v16l13 -8z"/></svg>''',
    "legacy/pause.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#ffffff" stroke="#ffffff" stroke-width="1"><rect x="6" y="5" width="4" height="14" rx="1"/><rect x="14" y="5" width="4" height="14" rx="1"/></svg>''',
    "legacy/stop.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#ffffff" stroke="#ffffff" stroke-width="1"><rect x="5" y="5" width="14" height="14" rx="2"/></svg>''',
    "legacy/rewind.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M20 4l-11 8l11 8v-16z"/><path d="M5 4v16"/></svg>''',
    "legacy/fastforward.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 4l11 8l-11 8v-16z"/><path d="M19 4v16"/></svg>''',
    "legacy/prev_keyframe.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M15 6l-6 6l6 6"/><path d="M19 6l-6 6l6 6"/></svg>''',
    "legacy/next_keyframe.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2.2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 18l6 -6l-6 -6"/><path d="M5 18l6 -6l-6 -6"/></svg>''',
    "legacy/preview.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 12v-6a2 2 0 0 1 2 -2h12a2 2 0 0 1 2 2v12a2 2 0 0 1 -2 2h-6"/><path d="M4 18l4 -3v6z"/></svg>''',
    "legacy/loop.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 12v-3a3 3 0 0 1 3 -3h13m-3 -3l3 3l-3 3"/><path d="M20 12v3a3 3 0 0 1 -3 3h-13m3 3l-3 -3l3 -3"/></svg>''',
    "legacy/visible.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M10 12a2 2 0 1 0 4 0a2 2 0 0 0 -4 0"/><path d="M21 12c-2.4 4 -5.4 6 -9 6c-3.6 0 -6.6 -2 -9 -6c2.4 -4 5.4 -6 9 -6c3.6 0 6.6 2 9 6"/></svg>''',
    "legacy/hidden.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M10.585 10.587a2 2 0 0 0 2.829 2.83"/><path d="M9.363 5.365a9.466 9.466 0 0 1 2.637 -.365c3.6 0 6.6 2 9 6c-.666 1.11 -1.379 2.067 -2.138 2.87m-3.486 1.946a9.458 9.458 0 0 1 -3.376 .189c-3.6 0 -6.6 -2 -9 -6c1.272 -2.12 2.712 -3.678 4.32 -4.674"/><path d="M3 3l18 18"/></svg>''',
    "legacy/locked.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11v-4a4 4 0 0 1 8 0v4"/><circle cx="12" cy="16" r="1"/></svg>''',
    "legacy/unlocked.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11v-5a4 4 0 0 1 8 0"/><circle cx="12" cy="16" r="1"/></svg>''',
    "legacy/record.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="#ff3b30" stroke="#ff3b30" stroke-width="1"><circle cx="12" cy="12" r="7"/></svg>''',
    "legacy/norecord.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2"><circle cx="12" cy="12" r="7"/></svg>''',
    "legacy/layer.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3l8 4.5l-8 4.5l-8 -4.5z"/><path d="M4 12l8 4.5l8 -4.5"/><path d="M4 16.5l8 4.5l8 -4.5"/></svg>''',
    "legacy/graph.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 3v18h18"/><path d="M4 18c4 0 6 -12 10 -12s4 8 7 8"/></svg>''',
    "legacy/preferences.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 10a2 2 0 1 0 4 0a2 2 0 0 0 -4 0"/><path d="M6 4v4"/><path d="M6 12v8"/><path d="M10 16a2 2 0 1 0 4 0a2 2 0 0 0 -4 0"/><path d="M12 4v10"/><path d="M12 18v2"/><path d="M16 7a2 2 0 1 0 4 0a2 2 0 0 0 -4 0"/><path d="M18 4v1"/><path d="M18 9v11"/></svg>''',
    "legacy/effect.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M15 4l6 2l-6 2l-2 6l-2 -6l-6 -2l6 -2l2 -6z"/><path d="M5 16l3 1l-3 1l-1 3l-1 -3l-3 -1l3 -1l1 -3z"/></svg>''',
    "legacy/duplicate.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="8" y="8" width="12" height="12" rx="2"/><path d="M16 8v-2a2 2 0 0 0 -2 -2h-8a2 2 0 0 0 -2 2v8a2 2 0 0 0 2 2h2"/></svg>''',
    "legacy/trash.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 7l16 0"/><path d="M10 11l0 6"/><path d="M14 11l0 6"/><path d="M5 7l1 12a2 2 0 0 0 2 2h8a2 2 0 0 0 2 -2l1 -12"/><path d="M9 7v-3a1 1 0 0 1 1 -1h4a1 1 0 0 1 1 1v3"/></svg>''',
    "legacy/plus.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 5l0 14"/><path d="M5 12l14 0"/></svg>''',
    "legacy/minus.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12l14 0"/></svg>''',
    "legacy/file_image.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="16" height="16" rx="2"/><circle cx="8.5" cy="8.5" r="1.5"/><path d="M20 15l-5 -5l-9 9"/><path d="M14 14l3 3"/></svg>''',
    "legacy/file_movie.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4" width="18" height="16" rx="2"/><path d="M7 4v16"/><path d="M17 4v16"/><path d="M3 8h4"/><path d="M3 12h18"/><path d="M3 16h4"/><path d="M17 8h4"/><path d="M17 16h4"/></svg>''',
    "legacy/file_sound.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 17a3 3 0 1 0 6 0a3 3 0 0 0 -6 0"/><path d="M9 17v-13h10v9"/><path d="M13 13a3 3 0 1 0 6 0a3 3 0 0 0 -6 0"/><path d="M9 8h10"/></svg>''',
    "legacy/file_folder.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 4h4l3 3h7a2 2 0 0 1 2 2v8a2 2 0 0 1 -2 2h-14a2 2 0 0 1 -2 -2v-11a2 2 0 0 1 2 -2z"/></svg>''',
    "legacy/linked.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M9 15l6 -6"/><path d="M11 6l.463 -.536a5 5 0 0 1 7.071 7.072l-.534 .464"/><path d="M13 18l-.397 .534a5.068 5.068 0 0 1 -7.127 0a4.972 4.972 0 0 1 0 -7.071l.524 -.463"/></svg>''',

    # Tools
    "friction-tools/select.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M6 3l12 11l-5.5 1l3.5 6l-2.5 1.5l-3.5 -6l-4 3.5z"/></svg>''',
    "friction-tools/pick.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M11 7l6 6"/><path d="M4 16l11.7 -11.7a1 1 0 0 1 1.4 0l2.6 2.6a1 1 0 0 1 0 1.4l-11.7 11.7h-4v-4z"/></svg>''',
    "friction-tools/pathCreate.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 20h4l10.5 -10.5a2.828 2.828 0 1 0 -4 -4l-10.5 10.5v4"/><path d="M13.5 6.5l4 4"/></svg>''',
    "friction-tools/drawPath.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 21l3 -1l11 -11l-2 -2l-11 11l-1 3"/><path d="M15 7l2 2"/></svg>''',
    "friction-tools/circleCreate.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/></svg>''',
    "friction-tools/rectCreate.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="16" height="16" rx="2"/></svg>''',
    "friction-tools/textCreate.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 7v-2h16v2"/><path d="M12 5v14"/><path d="M9 19h6"/></svg>''',
    "friction-tools/nullCreate.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="6" y="6" width="12" height="12" rx="1" stroke-dasharray="3 3"/><path d="M12 3v3"/><path d="M12 18v3"/><path d="M3 12h3"/><path d="M18 12h3"/></svg>''',
    "friction-tools/boxTransform.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="6" y="6" width="12" height="12"/><path d="M4 4h3v3h-3z"/><path d="M17 4h3v3h-3z"/><path d="M4 17h3v3h-3z"/><path d="M17 17h3v3h-3z"/></svg>''',
    "friction-tools/pivotMove.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="4"/><path d="M12 3v5"/><path d="M12 16v5"/><path d="M3 12h5"/><path d="M16 12h5"/></svg>''',
    "friction-tools/pivotGlobal.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="7"/><circle cx="12" cy="12" r="2"/><path d="M12 3v2"/><path d="M12 19v2"/><path d="M3 12h2"/><path d="M19 12h2"/></svg>''',

    # Friction
    "friction/range-in.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 4v16"/><path d="M9 12h11"/><path d="M13 8l-4 4l4 4"/></svg>''',
    "friction/range-out.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M19 4v16"/><path d="M4 12h11"/><path d="M11 8l4 4l-4 4"/></svg>''',
    "friction/range-clear.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 4l16 16"/><path d="M4 20l16 -16"/><path d="M4 12h16"/></svg>''',
    "friction/preview_loop.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M4 12v-3a3 3 0 0 1 3 -3h13m-3 -3l3 3l-3 3"/><path d="M20 12v3a3 3 0 0 1 -3 3h-13m3 3l-3 -3l3 -3"/></svg>''',
    "friction/grid.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="4" y="4" width="6" height="6" rx="1"/><rect x="14" y="4" width="6" height="6" rx="1"/><rect x="4" y="14" width="6" height="6" rx="1"/><rect x="14" y="14" width="6" height="6" rx="1"/></svg>''',
    "friction/memory.svg": '''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" stroke="#ffffff" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="6" y="6" width="12" height="12" rx="1"/><path d="M9 3v3"/><path d="M15 3v3"/><path d="M9 18v3"/><path d="M15 18v3"/><path d="M3 9h3"/><path d="M3 15h3"/><path d="M18 9h3"/><path d="M18 15h3"/></svg>'''
}

def write_svgs():
    print("Writing modernized Tabler-style SVGs...")
    for rel_path, svg_content in TABLER_SVGS.items():
        full_path = os.path.join(SCALABLE_DIR, rel_path)
        os.makedirs(os.path.dirname(full_path), exist_ok=True)
        with open(full_path, "w", encoding="utf-8") as f:
            f.write(svg_content.strip() + "\n")
    print(f"Updated {len(TABLER_SVGS)} SVG files.")

SIZES = [16, 17, 18, 20, 21, 22, 24, 25, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 64, 96, 128, 192, 256]
CATS = ["actions", "apps", "categories", "devices", "friction", "friction-tools", "friction-nodes", "legacy", "mimetypes", "places", "status"]

def render_icon(task):
    w, cat, svg_file, png_file = task
    cmd = [
        "rsvg-convert",
        "-w", str(w),
        "-h", str(w),
        "-f", "png",
        "-o", png_file,
        svg_file
    ]
    subprocess.run(cmd, check=True)

def render_all_pngs():
    tasks = []
    for cat in CATS:
        cat_svg_dir = os.path.join(SCALABLE_DIR, cat)
        if not os.path.isdir(cat_svg_dir):
            continue
        svgs = [f for f in os.listdir(cat_svg_dir) if f.endswith(".svg")]
        for w in SIZES:
            target_dir = os.path.join(HICOLOR_DIR, f"{w}x{w}", cat)
            os.makedirs(target_dir, exist_ok=True)
            for svg in svgs:
                svg_name = svg[:-4]
                svg_path = os.path.join(cat_svg_dir, svg)
                png_path = os.path.join(target_dir, f"{svg_name}.png")
                tasks.append((w, cat, svg_path, png_path))

    print(f"Rendering {len(tasks)} PNG icons in parallel via rsvg-convert...")
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 8) as executor:
        futures = [executor.submit(render_icon, t) for t in tasks]
        concurrent.futures.wait(futures)
    print("PNG rendering complete.")

def rebuild_qrc():
    print("Rebuilding hicolor.qrc...")
    qrc_path = os.path.join(CWD, "src/app/icons/hicolor.qrc")
    lines = ['<RCC>', ' <qresource prefix="/">', ' <file alias=\'icons/hicolor/index.theme\'>hicolor/index.theme</file>']
    for root, dirs, files in sorted(os.walk(HICOLOR_DIR)):
        for file in sorted(files):
            if file.endswith(".png"):
                full_path = os.path.join(root, file)
                rel_path = os.path.relpath(full_path, os.path.dirname(HICOLOR_DIR))
                lines.append(f"  <file alias='icons/{rel_path}'>{rel_path}</file>")
    lines.append(' </qresource>')
    lines.append('</RCC>')
    with open(qrc_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"Generated {qrc_path} with {len(lines) - 4} icon entries.")

if __name__ == "__main__":
    write_svgs()
    render_all_pngs()
    rebuild_qrc()
