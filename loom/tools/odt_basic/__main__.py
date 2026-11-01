"""python3 -m loom.tools.odt_basic PATH_OR_DASH LIMITS_JSON [IMPORT_CONTEXT_JSON]."""

import json
import sys

from .adapter import prepare_import
from .container import OdtError, check, fail, settings
from .reader import canonical, extract


def main(argv):
    try:
        if len(argv) not in (3, 4):
            fail('odt_configuration_invalid')
        limits = settings(json.loads(argv[2]))
        cap = limits['max_input_bytes']
        if argv[1] == '-':
            data = sys.stdin.buffer.read(cap + 1) if cap else sys.stdin.buffer.read()
        else:
            with open(argv[1], 'rb') as stream:
                data = stream.read(cap + 1) if cap else stream.read()
        if len(argv) == 4:
            context = json.loads(argv[3])
            result = prepare_import(data, limits, context['adapter'],
                                    source_key=context['source_key'], name=context['name'],
                                    language=context.get('language', ''))
        else:
            result = extract(data, limits)
        output = canonical(result)
        check(limits, 'max_output_bytes', len(output.encode('utf-8')) + 1)
        print(output)
        return 0
    except OdtError as error:
        code = error.code
    except OSError:
        code = 'odt_input_unavailable'
    except (ValueError, TypeError, KeyError):
        code = 'odt_configuration_invalid'
    print(json.dumps({'error': {'code': code}}, separators=(',', ':')))
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
