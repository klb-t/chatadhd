
import sys, argparse, logging
from db import DB
from engine.chat_engine import ChatEngine

logging.basicConfig(level=logging.INFO)
log = logging.getLogger('cli')

def main():
    parser = argparse.ArgumentParser(prog='chatadhd-cli')
    parser.add_argument('--model', default='local-echo')
    args = parser.parse_args()

    db = DB()
    engine = ChatEngine(config={'default_model': args.model}, secrets={}, db=db)

    print('ChatADHD CLI - graph memory demo')
    print('Commands: /help /quit /mem-add <text> [/parent=id] /mem-list /mem-tree /link <src> <dst> /select <query> /buffer-add <text> /buffer-show /buffer-clear /send <text>')

    while True:
        try:
            line = input('> ').strip()
        except EOFError:
            break
        if not line: continue
        if line.startswith('/'):
            parts = line.split(' ',2)
            cmd = parts[0]
            if cmd=='/help':
                print('See README for details.')
            elif cmd=='/quit':
                break
            elif cmd=='/mem-add':
                rest = line[len('/mem-add'):].strip()
                parent = None
                if '/parent=' in rest:
                    rest, p = rest.split('/parent=')
                    parent = p.strip()
                n = engine.add_memory(rest, parent)
                print('Added node', n.id)
            elif cmd=='/mem-list':
                for n in engine.memory.all_nodes():
                    print(f"{n.id} parent={n.parent} depth={n.depth} text={n.content[:80]}")
            elif cmd=='/mem-tree':
                print(engine.memory.to_text())
            elif cmd.startswith('/link'):
                _, src, dst = line.split(maxsplit=2)
                engine.link(src.strip(), dst.strip())
                print('Linked', src, '->', dst)
            elif cmd.startswith('/select'):
                q = line[len('/select'):].strip()
                sel = engine.get_relevant_memory(q, top_k=6)
                for s in sel:
                    print(f"{s.id} [{s.node_type}] ({s.created}) {s.content[:120]}")
            elif cmd.startswith('/buffer-add'):
                txt = line[len('/buffer-add'):].strip()
                sid = engine.buffer.add_stage(txt)
                print('Added stage', sid)
            elif cmd=='/buffer-show':
                for s in engine.buffer.list_stages():
                    print(s)
            elif cmd=='/buffer-clear':
                engine.buffer.stages = []
                print('Buffer cleared')
            elif cmd.startswith('/send'):
                txt = line[len('/send'):].strip()
                if not txt:
                    print('Provide text to send')
                else:
                    resp = engine.send(txt)
                    print('\n--- assistant ---\n' + resp + '\n')
            else:
                print('Unknown command. /help for help.')
        else:
            # send as message
            resp = engine.send(line)
            print('\n--- assistant ---\n' + resp + '\n')

if __name__ == '__main__':
    main()
