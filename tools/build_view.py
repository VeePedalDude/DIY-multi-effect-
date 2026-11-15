#!/usr/bin/env python3
"""tools/build_view.py views/<id>/ - check a view's manifest and pack it as dist/views/<id>.rigview (docs/VIEWS.md).

The bundle is a zip of manifest.json, view.js and view.css (if there is one). The page's checks
(66-views.js readRigview) are repeated here, so a bundle that builds also loads.
"""
import json, pathlib, re, sys, zipfile
root = pathlib.Path(__file__).resolve().parent.parent
src = pathlib.Path(sys.argv[1]).resolve()
m = json.loads((src / 'manifest.json').read_text())
fail = lambda why: sys.exit(f'{src.name}: {why}')
if m.get('format') != 'rigview' or m.get('formatVersion') != 1: fail('manifest: format must be "rigview", formatVersion 1')
if not re.fullmatch(r'[a-z0-9-]{1,40}', m.get('id', '')): fail('manifest: id must be 1-40 lowercase letters, digits or -')
if not isinstance(m.get('tab'), str) or not m['tab'].strip() or len(m['tab']) > 24: fail('manifest: tab must be 1-24 characters')
if m['id'] != src.name: fail(f'manifest: id "{m["id"]}" should match the folder name')
for f in m.get('feeds', []):
    if f not in ('notes', 'sources', 'levels', 'chain', 'presets', 'tempo'): fail(f'manifest: no feed called "{f}"')
for q in m.get('permissions', []):
    if q not in ('params', 'presets', 'tempo', 'chain'): fail(f'manifest: no permission called "{q}"')
if m.get('place', 'tab') not in ('tab', 'dock'): fail('manifest: place must be "tab" or "dock"')
if not (src / 'view.js').exists(): fail('no view.js')
out = root / 'dist/views' / f'{m["id"]}.rigview'; out.parent.mkdir(parents=True, exist_ok=True)
names = ['manifest.json', 'view.js'] + (['view.css'] if (src / 'view.css').exists() else [])
with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
    for n in names:
        info = zipfile.ZipInfo(n, date_time=(2026, 1, 1, 0, 0, 0)); info.compress_type = zipfile.ZIP_DEFLATED
        z.writestr(info, (src / n).read_bytes())                      # fixed dates: the same sources make the same file
size = out.stat().st_size
if size > 4 * 1024 * 1024: fail('the bundle is over 4 MB')
print(f'wrote {out.relative_to(root)} ({size // 1024} KB): {m["name"]} ({", ".join(names)})')
