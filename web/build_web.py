#!/usr/bin/env python3
"""build_web.py - inline the worklet, rig.wasm (base64) and app.js into dist/rig-test.html"""
import base64, pathlib
root = pathlib.Path(__file__).resolve().parent.parent
html = (root / 'web/index.html').read_text()
worklet = (root / 'web/engine-worklet.js').read_text()
app = (root / 'web/app.js').read_text()
wasm = base64.b64encode((root / 'build/rig.wasm').read_bytes()).decode()
for name, text in (('worklet', worklet), ('app', app)):
    assert '</script' not in text.lower(), f'{name} contains a closing script tag'
html = html.replace('/*__WORKLET__*/', worklet).replace('/*__WASM_B64__*/', wasm).replace('/*__APP__*/', app)
out = root / 'dist/rig-test.html'
out.parent.mkdir(exist_ok=True)
out.write_text(html)
print(f'wrote {out} ({out.stat().st_size // 1024} KB)')
