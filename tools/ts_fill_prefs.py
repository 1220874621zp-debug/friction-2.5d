# -*- coding: utf-8 -*-
"""One-shot preferences translation fill for friction_zh_CN.ts.

- Inserts brand-new <message> entries (GeneralSettingsWidget tooltips).
- Fills unfinished translations and drops the unfinished marker.
Text-mode editing to preserve the hand-maintained file formatting.
"""
import re
import sys

TS = 'src/app/translations/friction_zh_CN.ts'

def esc(s):
    return s.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')

# context -> {source: translation}
TABLE = {
'QObject': {
    'Default': '默认',
    'OK': '确定',
    'Cancel': '取消',
    'New Layers in Source PSD': '源 PSD 中的新图层',
    'The source PSD contains new layers.\nSelect the layers you want to import:': '源 PSD 包含新图层。\n请选择要导入的图层：',
},
'SettingsDialog': {
    'Theme & Appearance': '主题与外观',
    'Shortcuts': '快捷键',
    'Settings saved successfully': '设置已成功保存',
},
'GeneralSettingsWidget': {
    'Creates a backup file after each successful save.\n\nBackup files are stored in a folder called PROJECT.friction_backup.':
        '每次成功保存后创建备份文件。\n\n备份文件存放在名为 PROJECT.friction_backup 的文件夹中。',
    'Will auto save each X min if project is unsaved.\n\nEnable Backup on Save for incremental saves (and as a failsafe).':
        '工程有未保存更改时每隔 X 分钟自动保存。\n\n建议同时开启“保存时备份”，既可增量备份也是一道保险。',
    'If enabled the scaling factor for the UI will not be rounded, recommended for HiDPI displays.\nIf you use a negative font/display system scaling and/or see artifacts on UI elements then disable this option.\nThe scaling factor will then round up for .75 and above.':
        '开启后 UI 缩放系数不做取整，推荐 HiDPI 显示器使用。\n如果你使用负数的字体/显示系统缩放，或在界面元素上看到渲染瑕疵，请关闭此选项。\n关闭后缩放系数将对 0.75 及以上向上取整。',
    'PSD layer pixel cache. Safe to clear - layers currently in use are re-extracted from their .fpsd packages.':
        'PSD 图层像素缓存。可放心清除——正在使用的图层会从各自的 .fpsd 包中重新提取。',
    'Removed %1 cache file(s). Re-extracted %2 layer(s) currently in use.':
        '已删除 %1 个缓存文件。正在使用的 %2 个图层已重新提取。',
    'Snapshot PNGs are exported at 100% resolution. Empty folder means the Desktop is used.':
        '快照 PNG 以 100% 分辨率导出。文件夹留空表示使用桌面。',
},
'ThemeSettingsWidget': {
    "Click 'Live Preview' to inspect changes immediately without restarting.": '点击“实时预览”可立即查看改动效果，无需重启。',
    'Live Preview Theme': '实时预览主题',
    'Select any Morandi, Blender, or Friction theme card to apply full palette and styling:': '选择任意 Morandi、Blender 或 Friction 主题卡片，即可应用整套配色与样式：',
    'Base': '基础',
    'Alt': '交替',
    'Dark': '深色',
    'Accent': '强调',
    'Apply Theme': '应用主题',
    'Theme Gallery': '主题画廊',
    'Theme Preset': '主题预设',
    'Active Theme:': '当前主题：',
    'Accent & Highlight Color': '强调与高亮颜色',
    'Pick Custom...': '自定义颜色...',
    'Accent Preset:': '强调色预设：',
    'Background Tones (Custom Overrides)': '背景色调（自定义覆盖）',
    'Window Base Color:': '窗口基础色：',
    'Panel Alternate Color:': '面板交替色：',
    'Input / Darker Color:': '输入框/更深色：',
    'Reset Colors to Preset Defaults': '颜色重置为预设默认值',
    'Colors & Tones': '颜色与色调',
    'UI Geometry & Components': '界面几何与组件',
    'Sharp Corners (0px)': '直角（0px）',
    'Minimal Radius (2px)': '极小圆角（2px）',
    'Compact Radius (4px)': '紧凑圆角（4px）',
    'Standard Radius (6px)': '标准圆角（6px）',
    'Soft Rounded (8px)': '柔和圆角（8px）',
    'Large Radius (12px)': '大圆角（12px）',
    'Corner Radius:': '圆角半径：',
    'Hidden Scrollbar (0px)': '隐藏滚动条（0px）',
    'Slim Scrollbar (4px)': '纤细滚动条（4px）',
    'Standard Scrollbar (6px)': '标准滚动条（6px）',
    'Medium Scrollbar (8px)': '中等滚动条（8px）',
    'Large Scrollbar (12px)': '宽滚动条（12px）',
    'Scrollbar Width:': '滚动条宽度：',
    'UI & Controls': '界面与控件',
    'Custom Stylesheet (QSS Injection)': '自定义样式表（QSS 注入）',
    'Enter custom Qt Stylesheet (QSS) rules below to override or extend the active theme:': '在下方输入自定义 Qt 样式表（QSS）规则，覆盖或扩展现行主题：',
    'Clear Custom QSS': '清除自定义 QSS',
    'Custom QSS': '自定义 QSS',
    'Select Accent Color': '选择强调色',
    'Select Base Window Color': '选择窗口基础色',
    'Select Alternate Panel Color': '选择面板交替色',
    'Select Input / Darker Color': '选择输入框/更深色',
},
'ShortcutSettingsWidget': {
    'Object Mode (Select Tool)': '对象模式（选择工具）',
    'Tools': '工具',
    'Point Mode (Edit Points)': '点模式（编辑节点）',
    'Add Path (Pen Tool)': '添加路径（钢笔工具）',
    'Draw Path (Freehand)': '绘制路径（手绘）',
    'Add Circle (Shape)': '添加圆（形状）',
    'Add Rectangle (Shape)': '添加矩形（形状）',
    'Add Text': '添加文本',
    'Add Null Object': '添加空对象',
    'Color Pick Mode (Eyedropper)': '取色模式（吸管）',
    'Pivot Global / Local': '轴心 全局/局部',
    'Bookmark Current Color': '收藏当前颜色',
    'Go to First Frame': '跳到第一帧',
    'Timeline': '时间轴',
    'Go to Last Frame': '跳到最后一帧',
    'Show Anchor Point Property': '显示锚点属性',
    'Properties': '属性',
    'Show Position Property': '显示位置属性',
    'Show Scale Property': '显示缩放属性',
    'Show Rotation Property': '显示旋转属性',
    'Show Opacity Property': '显示不透明度属性',
    'Show Animated Properties (U)': '显示已动画属性（U）',
    'Quick Search Effects (AE: FX Console)': '快速搜索特效（AE：FX 控制台）',
    'Effects': '特效',
    'Full Screen Preview': '全屏预览',
    'View': '视图',
    'Command Palette': '命令面板',
    'Preview SVG': '预览 SVG',
    'File': '文件',
    'Export SVG': '导出 SVG',
    'Add to Render Queue': '添加到渲染队列',
    'Scene': '场景',
    'New Scene (AE: Composition)': '新建场景（AE：合成）',
    'Scene Settings (AE: Comp Settings)': '场景设置（AE：合成设置）',
    'Duplicate Layer': '复制图层',
    'Layer': '图层',
    'Split Layer at Playhead': '在播放头处拆分图层',
    'Group Layers (AE: Pre-compose)': '图层编组（AE：预合成）',
    'Rename Layer': '重命名图层',
    'Raise Layer One Level': '图层上移一层',
    'Lower Layer One Level': '图层下移一层',
    'Go to Next Keyframe': '跳到下一关键帧',
    'Go to Previous Keyframe': '跳到上一关键帧',
    'Go to Layer In-Point': '跳到图层入点',
    'Go to Layer Out-Point': '跳到图层出点',
    'Set Work Area Start': '设置工作区起点',
    'Set Work Area End': '设置工作区终点',
    'Step One Frame Back': '后退一帧',
    'Step One Frame Forward': '前进一帧',
    'Preset': '预设',
    'Default (Friction)': '默认（Friction）',
    'AE (After Effects)': 'AE（After Effects）',
    'Apply Preset': '应用预设',
    'Action': '操作',
    'Category': '分类',
    'AE Default': 'AE 默认',
    'Shortcut': '快捷键',
    'Changes take effect after restart. Entries marked [reserved] are planned features and will be bound in a future version.':
        '改动重启后生效。标记为 [reserved] 的条目是规划中的功能，将在后续版本绑定。',
    'reserved': '保留',
},
'TimelineDockWidget': {
    'Export the current frame as a 100% resolution PNG (snapshot path is configurable in Preferences, default: Desktop)':
        '将当前帧导出为 100% 分辨率的 PNG（快照路径可在首选项中配置，默认：桌面）',
    'Auto freeze pose: key ALL bone channels on every pose edit (pinned poses, no drift)':
        '自动冻结姿势：每次编辑姿势时为骨骼所有通道打关键帧（姿势固定、不漂移）',
    'Cycle keyframed animation forward after the last key (1,2,3 -> 1,2,3,1,...); applies a loop expression to every keyed property of the selected layers, bones included; click again to remove':
        '在最后一个关键帧后正向循环动画（1,2,3 -> 1,2,3,1,...）；为选中图层的所有已打帧属性应用循环表达式（含骨骼）；再次点击可移除',
    'Bounce keyframed animation back and forth after the last key (1,2,3 -> 1,2,3,2,1,...); applies a loop expression to every keyed property of the selected layers, bones included; click again to remove':
        '在最后一个关键帧后往复循环动画（1,2,3 -> 1,2,3,2,1,...）；为选中图层的所有已打帧属性应用循环表达式（含骨骼）；再次点击可移除',
    'Cycle keyframed animation skipping the given number of leading keys (keys 1,2,3, skip 1 -> cycles 2,3); the amount is asked for when enabled; applies a loop expression to every keyed property of the selected layers, bones included; click again to remove':
        '跳过开头指定数量的关键帧后循环（帧 1,2,3，跳过 1 -> 循环 2,3）；启用时会询问跳过数量；为选中图层的所有已打帧属性应用循环表达式（含骨骼）；再次点击可移除',
    'Set Layer In Point (Alt+[)': '设置图层入点 (Alt+[',
    'Set Layer Out Point (Alt+])': '设置图层出点 (Alt+])',
    'Split Clip (Ctrl+Shift+D)': '拆分片段 (Ctrl+Shift+D)',
    'Split Clip at Current Frame': '在当前帧拆分片段',
    'No rendered frame available yet - wait for the preview to render this frame':
        '暂无已渲染帧——请等待预览渲染完此帧',
    'Snapshot timed out - the frame did not render in time':
        '快照超时——帧未能及时渲染完成',
},
'EffectsPresetsPanel': {
    'Search Effects & Presets...': '搜索特效与预设...',
    'Import...': '导入...',
    'Import custom GLSL shader effect (.frag / .json)': '导入自定义 GLSL 着色器特效（.frag / .json）',
    'Folder': '文件夹',
    'Open custom effects & shaders directory': '打开自定义特效与着色器目录',
    'Refresh': '刷新',
    'Reload shader effects and presets': '重新加载着色器特效与预设',
    'General': '通用',
    'Custom': '自定义',
    'Shader': '着色器',
},
}

ctx_re = re.compile(r'<context>\s*<name>([^<]+)</name>.*?</context>', re.DOTALL)

def main():
    text = open(TS, encoding='utf-8').read()
    fixed = 0
    inserted = 0
    out = []
    pos = 0
    for m in ctx_re.finditer(text):
        name = m.group(1)
        out.append(text[pos:m.start()])
        block = m.group(0)
        table = TABLE.get(name)
        if table:
            for src, tr in table.items():
                pat_s = (r'(<source>%s</source>\s*<translation)'
                         r'(?:\s+type="unfinished")?[^>]*>(?:[^<]*)?(</translation>|/>)'
                         ) % re.escape(esc(src))
                pat = re.compile(pat_s, re.DOTALL)
                new_trans = '<source>%s</source>\n        <translation>%s</translation>' % (esc(src), esc(tr))
                block2, n = pat.subn(new_trans, block, count=1)
                if n:
                    fixed += 1
                    block = block2
            # insert any sources that had no entry at all
            for src, tr in table.items():
                if ('<source>%s</source>' % esc(src)) not in block:
                    entry = ('    <message>\n'
                             '        <source>%s</source>\n'
                             '        <translation>%s</translation>\n'
                             '    </message>\n' % (esc(src), esc(tr)))
                    block = block.replace('</context>', entry + '</context>')
                    inserted += 1
        out.append(block)
        pos = m.end()
    out.append(text[pos:])
    open(TS, 'w', encoding='utf-8', newline='').write(''.join(out))
    print('finished/filled:', fixed, ' inserted:', inserted)

if __name__ == '__main__':
    main()
