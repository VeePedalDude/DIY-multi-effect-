#!/usr/bin/env python3
"""tools/build_web.py - assemble dist/rig-test.html from ui/ and build/rig.wasm.

Script order: ui/js/*.js by name, with every ui/screens/*.js inserted before the
first ui/js file numbered 50 or higher. All of it runs inside one function scope.
"""
import base64, pathlib, re, sys
root = pathlib.Path(__file__).resolve().parent.parent
ui = root / 'ui'
js = sorted((ui / 'js').glob('*.js'))
screens = sorted((ui / 'screens').glob('*.js'))
early = [p for p in js if int(re.match(r'(\d+)', p.name).group(1)) < 50]
late = [p for p in js if p not in early]
parts = early + screens + late
app = "(function () {\n'use strict';\n" + "\n".join(f"// ===== {p.relative_to(root)} =====\n{p.read_text()}" for p in parts) + "\n})();\n"
worklet = (ui / 'worklet/engine-worklet.js').read_text()
wasm = base64.b64encode((root / 'build/rig.wasm').read_bytes()).decode()
for name, text in (('worklet', worklet), ('app', app)):
    if '</script' in text.lower():
        sys.exit(f'{name} contains a closing script tag')
html = (ui / 'index.html').read_text()
for ph in ('/*__WORKLET__*/', '/*__WASM_B64__*/', '/*__APP__*/'):
    if ph not in html: sys.exit(f'placeholder {ph} missing from ui/index.html')
html = html.replace('/*__WORKLET__*/', worklet).replace('/*__WASM_B64__*/', wasm).replace('/*__APP__*/', app)
(root / 'build').mkdir(exist_ok=True)
(root / 'build/app.js').write_text(app)          # for syntax checks
out = root / 'dist/rig-test.html'; out.parent.mkdir(exist_ok=True); out.write_text(html)
print(f'wrote {out.relative_to(root)} ({out.stat().st_size // 1024} KB): {len(parts)} ui files ({len(screens)} screens)')
