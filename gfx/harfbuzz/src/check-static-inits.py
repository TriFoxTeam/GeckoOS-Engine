#!/usr/bin/env python3

import sys, os, shutil, subprocess, glob, re

builddir = os.getenv ('builddir', os.path.dirname (__file__))
libs = os.getenv ('libs', '.libs')

objdump = os.getenv ('OBJDUMP', shutil.which ('objdump'))
if not objdump:
	print ('check-static-inits.py: \'ldd\' not found; skipping test')
	sys.exit (77)

if sys.version_info < (3, 5):
	print ('check-static-inits.py: needs python 3.5 for recursive support in glob')
	sys.exit (77)

OBJS = glob.glob (os.path.join (builddir, libs, '**', '*hb*.o'), recursive=True)
if not OBJS:
	print ('check-static-inits.py: object files not found; skipping test')
	sys.exit (77)

stat = 0
tested = 0

result = subprocess.run(objdump.split () + ['-t'] + OBJS, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

if result.returncode:
    if result.stderr.find (b'not recognized') != -1:
        # https://github.com/harfbuzz/harfbuzz/issues/3019
        print ('objdump %s returned "not recognized", skipping')
    else:
        print ('objdump returned error:\n%s' % (result.stderr.decode ('utf-8')))
        stat = 2
else:
    tested = 1

result = result.stdout.decode ('utf-8')

# Checking that no object file has static initializers
for l in re.findall (r'^.*\.[cd]tors.*$', result, re.MULTILINE):
    if not re.match (r'.*\b0+\b', l):
        print ('Ouch, library has static initializers/finalizers')
        stat = 1

# Checking that no object file has lazy static C++ constructors/destructors or other such stuff
if ('__cxa_' in result) and ('__ubsan_handle' not in result):
    print ('Ouch, library has lazy static C++ constructors/destructors or other such stuff')
    stat = 1


sys.exit (stat if tested else 77)
