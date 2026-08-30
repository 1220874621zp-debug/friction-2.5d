import re, xml.etree.ElementTree as ET
import sys

files = {
 'SettingsDialog': 'src/app/GUI/Settings/settingsdialog.cpp',
 'GeneralSettingsWidget': 'src/app/GUI/Settings/generalsettingswidget.cpp',
 'ThemeSettingsWidget': 'src/app/GUI/Settings/themesettingswidget.cpp',
 'ShortcutSettingsWidget': 'src/app/GUI/Settings/shortcutsettingswidget.cpp',
 'PluginsSettingsWidget': 'src/app/GUI/Settings/pluginssettingswidget.cpp',
 'TimelineSettingsWidget': 'src/app/GUI/Settings/timelinesettingswidget.cpp',
 'CanvasSettingsWidget': 'src/ui/widgets/canvassettingswidget.cpp',
 'PerformanceSettingsWidget': 'src/ui/widgets/performancesettingswidget.cpp',
 'PresetSettingsWidget': 'src/ui/widgets/presetsettingswidget.cpp',
}
# argv items override/add: Context=path (repeatable)
for arg in sys.argv[1:]:
    if '=' in arg:
        ctx, path = arg.split('=', 1)
        files[ctx] = path
tr_re = re.compile(r'\btr\(\s*"((?:[^"\\]|\\.)*)"')
sources = {}
for ctx, path in files.items():
    try:
        text = open(path, encoding='utf-8').read()
    except FileNotFoundError:
        print(f'{ctx}: FILE MISSING {path}')
        continue
    sources[ctx] = tr_re.findall(text)

tree = ET.parse('src/app/translations/friction_zh_CN.ts')
have = {}
for ctxel in tree.getroot().findall('context'):
    name = ctxel.find('name').text
    s = set()
    for m in ctxel.findall('message'):
        src = m.find('source').text
        tr = m.find('translation')
        if tr is not None and (tr.text or '').strip() and tr.get('type') != 'unfinished':
            s.add(src)
    have[name] = s

for ctx, srcs in sources.items():
    missing = [x for x in srcs if x not in have.get(ctx, set())]
    if missing:
        print(f'== {ctx}: {len(missing)} missing')
        for m in missing:
            print('    ' + repr(m))
