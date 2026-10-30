#!/usr/bin/env python3
"""tools/gen_registry.py - make engine/modules/registry.h and engine/sources/registry.h from the folders.

The file system is the registry: every header in a category folder (engine/modules/<category>/<id>.h,
engine/sources/<category>/<id>.h) is one type. categories.txt in each tree lists the categories (the
tabs of the add panels) in order; default_chain.txt / default_rack.txt list what a fresh rig starts with.
Inside a tab, types are listed by id, unless the folder has an order.txt: the ids it names come first,
in that order (simple to sophisticated, say), then any others by id.

Checks, each failing with a message that says what to fix:
  - a header sits in a folder that categories.txt doesn't list, or loose in the tree's top folder
  - a header doesn't declare exactly one `struct <Type> : Module` (or `: Source`)
  - its description's id isn't its file name (holo.h -> "holo"), or isn't [a-z][a-z0-9_]*
  - two types share an id or a struct name
  - a default list or an order.txt names an id that doesn't exist (there), or one twice

Usage: gen_registry.py [--check] [root]   (--check: fail if a registry.h is out of date, write nothing)
"""
import os, re, sys

KINDS = [
    # tree, base struct, desc type, macro, categories macro, default file, default macro, fresh limit
    ('modules', 'Module', 'ModuleDesc', 'RIG_MODULES', 'RIG_MODULE_CATEGORIES', 'default_chain.txt', 'RIG_DEFAULT_CHAIN', 12),
    ('sources', 'Source', 'SourceDesc', 'RIG_SOURCES', 'RIG_SOURCE_CATEGORIES', 'default_rack.txt', 'RIG_DEFAULT_RACK', 16),
]
ID = re.compile(r'^[a-z][a-z0-9_]*$')

class RegistryError(Exception):
    pass

def read_categories(path):
    cats = []
    if not os.path.isfile(path):
        raise RegistryError(f'{path} is missing: it lists the categories (folders) in order')
    for n, line in enumerate(open(path, encoding='utf-8'), 1):
        line = line.split('#', 1)[0].strip() if line.lstrip().startswith('#') else line.strip()
        if not line:
            continue
        parts = [p.strip() for p in line.split('|')]
        if len(parts) != 3 or not ID.match(parts[0]) or not parts[1]:
            raise RegistryError(f'{path}:{n}: expected "folder | tab label | what goes here", got "{line}"')
        if any(c[0] == parts[0] for c in cats):
            raise RegistryError(f'{path}:{n}: category "{parts[0]}" listed twice')
        cats.append(tuple(parts))
    if not cats:
        raise RegistryError(f'{path} lists no categories')
    return cats

def read_defaults(path):
    if not os.path.isfile(path):
        raise RegistryError(f'{path} is missing: it lists what a fresh rig starts with')
    ids = []
    for line in open(path, encoding='utf-8'):
        if not line.lstrip().startswith('#'):
            ids += line.split()
    return ids

def scan(tree, base, desc_type, defaults_file):
    """-> (categories, types [(struct, id, category, relpath)], defaults)"""
    cats = read_categories(os.path.join(tree, 'categories.txt'))
    names = [c[0] for c in cats]
    types = []
    for entry in sorted(os.listdir(tree)):
        full = os.path.join(tree, entry)
        if os.path.isfile(full):
            if entry.endswith('.h') and entry != 'registry.h':
                raise RegistryError(f'{full}: put it in a category folder ({", ".join(names)}), e.g. {tree}/{names[0]}/{entry}')
            continue
        if entry not in names:
            if any(f.endswith('.h') for f in os.listdir(full)):
                raise RegistryError(f'{full}/ isn\'t a category in {tree}/categories.txt (add a line for it, or move its files to one of: {", ".join(names)})')
            continue
    for cat in names:
        folder = os.path.join(tree, cat)
        if not os.path.isdir(folder):
            continue
        found = []
        for f in sorted(os.listdir(folder)):
            if not f.endswith('.h'):
                continue
            path = os.path.join(folder, f)
            src = open(path, encoding='utf-8').read()
            structs = re.findall(r'\bstruct\s+(\w+)\s*(?:final\s*)?:\s*(?:public\s+)?' + base + r'\b', src)
            if len(structs) != 1:
                raise RegistryError(f'{path}: needs exactly one `struct <Name> : {base}` (found {len(structs)}). One type per file.')
            m = re.search(desc_type + r'\s+\w+\s*=\s*\{\s*"([^"]*)"', src)
            if not m:
                raise RegistryError(f'{path}: no `inline constexpr {desc_type} ... = {{"<id>", ...}}` found')
            tid, stem = m.group(1), f[:-2]
            if not ID.match(tid):
                raise RegistryError(f'{path}: id "{tid}" must be [a-z][a-z0-9_]*')
            if tid != stem:
                raise RegistryError(f'{path}: its id is "{tid}" but the file is {f}; name the file {tid}.h (or the id "{stem}")')
            for t in types:
                if t[1] == tid:
                    raise RegistryError(f'{path}: id "{tid}" is already used by {t[3]}')
                if t[0] == structs[0]:
                    raise RegistryError(f'{path}: struct {structs[0]} is already declared in {t[3]}')
            found.append((structs[0], tid, cat, f'{cat}/{f}'))
        order_file = os.path.join(folder, 'order.txt')
        if os.path.isfile(order_file):
            order = read_defaults(order_file)
            for i, o in enumerate(order):
                if not any(t[1] == o for t in found):
                    raise RegistryError(f'{order_file}: "{o}" isn\'t in {folder}/ (have: {", ".join(t[1] for t in found)})')
                if o in order[:i]:
                    raise RegistryError(f'{order_file}: "{o}" listed twice')
            found.sort(key=lambda t: (order.index(t[1]) if t[1] in order else len(order), t[1]))
        types += found
    if not types:
        raise RegistryError(f'{tree}: no types found')
    defaults = read_defaults(os.path.join(tree, defaults_file))
    for i, d in enumerate(defaults):
        if not any(t[1] == d for t in types):
            raise RegistryError(f'{tree}/{defaults_file}: "{d}" isn\'t a {base.lower()} id (have: {", ".join(t[1] for t in types)})')
        if d in defaults[:i]:
            raise RegistryError(f'{tree}/{defaults_file}: "{d}" listed twice')
    return cats, types, defaults

def render(kind, cats, types, defaults):
    tree, base, _, macro, cmacro, dfile, dmacro, limit = kind
    w = max(len(t[0]) for t in types)
    out = [f'// engine/{tree}/registry.h - GENERATED by tools/gen_registry.py from the folders in engine/{tree}/. Do not edit.',
           f'// To add a {base.lower()}: put engine/{tree}/<category>/<id>.h in a folder listed in categories.txt, then',
           f'// run ./build.sh. To change what a fresh rig starts with: {dfile}.',
           '#pragma once']
    out += [f'#include "{t[3]}"' for t in types]
    out += ['', f'// X(Type, id, category): every {base.lower()} type, by category (the order of categories.txt), then by id or order.txt',
            f'#define {macro}(X) \\']
    out += [f'  X({(t[0] + ",").ljust(w + 1)} {t[1]}, {t[2]}) \\' for t in types]
    out[-1] = out[-1].rstrip(' \\')
    esc = lambda s: s.replace('\\', '\\\\').replace('"', '\\"')
    out += ['', '// X(id, "tab label", "what goes here"): the categories, in tab order', f'#define {cmacro}(X) \\']
    out += [f'  X({c[0]}, "{esc(c[1])}", "{esc(c[2])}") \\' for c in cats]
    out[-1] = out[-1].rstrip(' \\')
    out += ['', f'// a fresh rig, in order ({dfile})', f'#define {dmacro} ' + ', '.join(f'"{d}"' for d in defaults), '']
    return '\n'.join(out)

def generate(root='.', check=False):
    """-> list of (path, changed). Raises RegistryError."""
    results = []
    for kind in KINDS:
        tree = os.path.join(root, 'engine', kind[0])
        cats, types, defaults = scan(tree, kind[1], kind[2], kind[5])
        if len(defaults) > kind[7]:
            raise RegistryError(f'{tree}/{kind[5]}: {len(defaults)} ids, but a fresh rig holds at most {kind[7]}')
        text = render(kind, cats, types, defaults)
        path = os.path.join(tree, 'registry.h')
        old = open(path, encoding='utf-8').read() if os.path.isfile(path) else None
        changed = old != text
        if changed and not check:
            open(path, 'w', encoding='utf-8').write(text)
        results.append((path, changed, len(types), len(cats)))
    return results

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--check']
    check = '--check' in sys.argv
    try:
        res = generate(args[0] if args else '.', check)
    except RegistryError as e:
        print(f'registry: {e}', file=sys.stderr)
        sys.exit(1)
    for path, changed, n, c in res:
        print(f'   {path}: {n} types in {c} categories' + (' (updated)' if changed and not check else ' (OUT OF DATE)' if changed else ''))
    if check and any(r[1] for r in res):
        sys.exit(1)
