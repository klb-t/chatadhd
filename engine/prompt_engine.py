from typing import List, Any, Optional

def nodes_to_prompt_text(nodes: List[Any], include_meta=True) -> str:
    blocks = []
    for n in nodes:
        h = f"[{n.node_type.upper()}] id={n.id}"
        if include_meta and n.metadata.get('source'): h += f" src={n.metadata['source']}"
        if include_meta: h += f" ts={n.created}"
        conf = n.metadata.get('confidence')
        if conf is not None:
            h += f" [{'HIGH' if conf>=0.75 else 'MED' if conf>=0.4 else 'LOW'}:{conf:.2f}]"
        body = (n.content or '').strip()
        if n.links: body += '\n(links: ' + ','.join(list(n.links)[:5]) + ')'
        blocks.append(h + '\n' + body)
    return '\n\n'.join(blocks)

def build_system_prompt(memory_text: str, instructions: Optional[str] = None) -> str:
    return "You are an assistant with access to user's memory.\n" + (instructions or '') + "\n\nMemory:\n" + memory_text
