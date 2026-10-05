#!/usr/bin/env python3
# The only edits the simulator makes to the firmware text (wasm has no C++ exceptions here):
#   the one try/catch around the library build becomes a plain block (out-of-memory aborts instead).
import sys, re
s = open(sys.argv[1], encoding='utf-8').read()
n_try = s.count('    try{\n'); n_thr = s.count('throw std::bad_alloc();'); n_cat = s.count('}catch(...){')
assert (n_try, n_thr, n_cat) == (1, 1, 1), (n_try, n_thr, n_cat)
s = s.replace('    try{\n', '    {\n').replace('throw std::bad_alloc();', '__sim_oom();').replace('}catch(...){', '} if(0){')
open(sys.argv[2], 'w', encoding='utf-8').write(s)
print('sim_sketch: ok')
