"""tests/tools/gen_registry.test.py - the folder-based registry (tools/gen_registry.py): it builds the
registry from category folders, and refuses each kind of mistake with a message that says what to fix."""
import os, shutil, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', 'tools'))
import gen_registry as g

fails = 0
def check(ok, msg):
    global fails
    print(f"  [{'PASS' if ok else 'FAIL'}] {msg}")
    if not ok: fails += 1

ROOT = os.path.join(os.path.dirname(__file__), '..', '..')
MOD = 'struct {S} : Module {{ }};\ninline constexpr ModuleDesc {S}_DESC = {{"{id}", "{S}"}};\n'
SRC = 'struct {S} : Source {{ }};\ninline constexpr SourceDesc {S}_DESC = {{"{id}", "{S}"}};\n'

def tree():
    """A copy of the real engine/modules and engine/sources folders (headers, lists) in a temp dir."""
    d = tempfile.mkdtemp()
    for kind in ('modules', 'sources'):
        shutil.copytree(os.path.join(ROOT, 'engine', kind), os.path.join(d, 'engine', kind))
    return d
def put(d, rel, text):
    p = os.path.join(d, 'engine', rel); os.makedirs(os.path.dirname(p), exist_ok=True); open(p, 'w').write(text)
def err(d):
    try: g.generate(d); return None
    except g.RegistryError as e: return str(e)

print('folder-based registry')
d = tree(); res = g.generate(d)
real = open(os.path.join(ROOT, 'engine/modules/registry.h')).read()
check(open(os.path.join(d, 'engine/modules/registry.h')).read() == real, 'the real folders give the registry that is checked in')
put(d, 'modules/modulation/trem.h', MOD.format(S='Trem', id='trem'))
put(d, 'sources/chaos/walk.h', SRC.format(S='Walk', id='walk'))
g.generate(d)
reg = open(os.path.join(d, 'engine/modules/registry.h')).read(); sreg = open(os.path.join(d, 'engine/sources/registry.h')).read()
check('#include "modulation/trem.h"' in reg and 'X(Trem,' in reg and 'trem, modulation)' in reg and 'X(Walk,' in sreg,
      'dropping a file into a category folder adds it, with its category; nothing else to edit')
check('"trem"' not in reg.split('RIG_DEFAULT_CHAIN')[1], 'a new effect is not added to the fresh chain')
shutil.rmtree(d)

print('mistakes are refused, with the fix')
cases = [
    ('a file loose in engine/modules/', lambda d: put(d, 'modules/fuzz.h', MOD.format(S='Fuzz', id='fuzz')), 'put it in a category folder'),
    ('a folder that isn\'t a category', lambda d: put(d, 'modules/wobble/x.h', MOD.format(S='X', id='x')), "isn't a category"),
    ('two structs in one file', lambda d: put(d, 'modules/delay/two.h', MOD.format(S='A', id='two') + 'struct B : Module {};'), 'exactly one'),
    ('id differs from the file name', lambda d: put(d, 'modules/delay/echo.h', MOD.format(S='Echo', id='ekko')), 'name the file ekko.h'),
    ('an id already used', lambda d: put(d, 'modules/reverb/holo.h', MOD.format(S='Holo2', id='holo')), 'already used'),
    ('a struct name already used', lambda d: put(d, 'modules/reverb/room.h', MOD.format(S='Holo', id='room')), 'already declared'),
    ('a bad id', lambda d: put(d, 'modules/delay/Big.h', MOD.format(S='Big', id='Big')), 'must be [a-z]'),
    ('a default chain naming a missing id', lambda d: open(os.path.join(d, 'engine/modules/default_chain.txt'), 'a').write('\nflanger\n'), '"flanger" isn\'t a module id'),
    ('a bad categories line', lambda d: open(os.path.join(d, 'engine/sources/categories.txt'), 'a').write('\nwobble only two | fields\n'), 'expected "folder | tab label'),
]
for what, mutate, want in cases:
    d = tree(); mutate(d); e = err(d); shutil.rmtree(d)
    check(e is not None and want in e, f'{what}: “{e}”')

print('gen_registry: ' + ('all passed' if not fails else f'{fails} FAILED'))
sys.exit(1 if fails else 0)
