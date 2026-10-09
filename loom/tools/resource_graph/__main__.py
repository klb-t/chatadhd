"""Run from the repository root: python -m loom.tools.resource_graph --help."""
import argparse
import json
from pathlib import Path
from .core import ResourceGraph
from .access import Access


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('describe','select','project','discover','recognize'):
        command = sub.add_parser(name)
        command.add_argument('source')
        command.add_argument('--id', default='cli-resource')
        command.add_argument('--member', action='append', default=[])
        command.add_argument('--format')
        command.add_argument('--selector', default='')
        command.add_argument('--depth', type=int)
        command.add_argument('--allow-origin', action='append', default=[])
    demo = sub.add_parser('demo')
    demo.add_argument('--output', required=True)
    demo.add_argument('--library')
    args = parser.parse_args()
    if args.command == 'demo':
        from .scenario import run
        result = run(Path(args.output), library=args.library)
    else:
        graph = ResourceGraph(access=Access({'allowed_origins':args.allow_origin}))
        graph.attach(args.source,logical_id=args.id,members=args.member,format=args.format)
        if args.command == 'describe': result = graph.describe(args.id)
        elif args.command == 'select': result = graph.select(args.id,args.selector)
        elif args.command == 'project': result = graph.project(args.id,args.selector,depth=args.depth)
        elif args.command == 'recognize': result = graph.recognize(args.id)
        else: result = graph.discover(args.id)
    print(json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False))


if __name__ == '__main__':
    main()
