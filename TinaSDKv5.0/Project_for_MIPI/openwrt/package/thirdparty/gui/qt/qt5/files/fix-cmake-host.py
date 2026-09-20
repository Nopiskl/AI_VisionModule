#!/usr/bin/env python3
"""Relocate Qt CMake host tools and the cross mkspec after SDK InstallDev."""
from pathlib import Path
import sys,os
base=Path(sys.argv[1]);old=sys.argv[2];host=Path(sys.argv[3]).resolve()
for p in base.rglob('*.cmake'):
 t=p.read_text()
 rel=os.path.relpath(str(host),str(p.parent.resolve()))
 relocated='$'+'{CMAKE_CURRENT_LIST_DIR}/'+rel
 # Qt emits tools with absolute hostprefix, but mkspecs relative to target prefix.
 fixed=t.replace(old,relocated)
 fixed=fixed.replace('$'+'{_qt5Core_install_prefix}/../host/',relocated+'/')
 if fixed!=t:p.write_text(fixed)
