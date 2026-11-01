"""Check global C++ logger entry points in the independently linked probe.
This is not the full libloom C ABI gate; no C ABI addition is made here.
"""
import subprocess
import sys

output = subprocess.check_output([sys.argv[1], '-C', '--defined-only', sys.argv[2]], text=True)
symbols = [line.split(' T ', 1)[1] for line in output.splitlines() if ' T ' in line]
required = ['loom::log::set_enabled(bool)', 'loom::log::is_enabled()',
            'loom::log::set_level(loom::log::Level)', 'loom::log::set_stderr(bool)',
            'loom::log::recent[abi:cxx11](unsigned long)',
            'loom::log::enabled(loom::log::Level)']
for symbol in required:
    if symbol not in symbols:
        raise AssertionError(f'missing global symbol: {symbol}')
if not any(symbol.startswith('loom::log::write_checked(') for symbol in symbols):
    raise AssertionError('missing global write_checked')
print('PASS 7/7 global C++ symbols (2 new, 5 legacy); full shared C ABI not tested')
