
from typing import List, Any, Optional
import logging
log = logging.getLogger('prompt_engine')

def nodes_to_prompt_text(nodes: List[Any], include_meta: bool = True) -> str:
    blocks = []
    for n in nodes:
        header = f"[{n.node_type.upper()}] id={n.id}"                 + (f" source={n.metadata.get('source')}" if include_meta and n.metadata.get('source') else '')                 + (f" ts={n.created}" if include_meta else '')
        conf = ''
        if n.metadata.get('confidence') is not None:
            confv = n.metadata.get('confidence')
            tag = 'HIGH' if confv>=0.75 else 'MED' if confv>=0.4 else 'LOW'
            conf = f" [CONF={tag}:{confv}]"
        body = (n.content or '').strip()
        if n.links:
            body += '\n(links: ' + ','.join(list(n.links)[:5]) + ')'
        blocks.append(header + conf + '\n' + body)
    return '\n\n'.join(blocks)

def build_system_prompt(memory_text: str, instructions: Optional[str]=None) -> str:
    intro = "You are an assistant with access to a user's memory. Use only the most relevant items below.\n"
    return intro + (instructions or '') + "\n\nMemory:\n" + memory_text
