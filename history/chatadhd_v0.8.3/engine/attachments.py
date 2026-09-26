"""Attachment ingestion, representation and prompt-view building.

Best-effort pipeline:
  source file -> resource node -> semantic representations -> prompt view

The design deliberately separates:
- physical resource metadata
- derived representations
- query-aware selection for LLM context

This is a compact implementation for v0.8.0, using mostly stdlib and
optional parsers when available.
"""
from __future__ import annotations

import base64
import csv
import gzip
import hashlib
import io
import json
import logging
import mimetypes
import os
import tarfile
import zipfile
from dataclasses import dataclass, asdict, field
from pathlib import Path
from typing import Any, Iterable, Optional
from xml.etree import ElementTree as ET

log = logging.getLogger(__name__)

_IMAGE_EXTS = {".png", ".jpg", ".jpeg", ".gif", ".webp", ".bmp"}
_TEXT_EXTS = {
    ".txt", ".py", ".md", ".json", ".csv", ".xml", ".html", ".css", ".js", ".ts",
    ".yaml", ".yml", ".toml", ".ini", ".cfg", ".sh", ".bat", ".rs", ".go", ".java",
    ".c", ".cpp", ".h", ".sql", ".kt", ".swift", ".php", ".rb", ".lua", ".r",
    ".pl", ".ps1", ".tsx", ".jsx", ".env", ".log", ".rst",
}
_DOC_EXTS = {".pdf", ".docx", ".odt", ".rtf", ".epub", ".html", ".htm", ".mhtml"}
_SHEET_EXTS = {".xlsx", ".xls", ".ods", ".csv", ".tsv"}
_SLIDE_EXTS = {".pptx", ".odp"}
_ARCHIVE_EXTS = {".zip", ".tar", ".gz", ".tgz", ".tar.gz", ".bz2", ".xz", ".7z", ".rar"}
_CONTAINER_EXTS = {".zip", ".docx", ".xlsx", ".pptx", ".odt", ".ods", ".odp", ".epub"}
_SKIP_DIR_NAMES = {"__pycache__", ".git", ".venv", "venv", "node_modules", "build", "dist", ".idea"}
_MAX_TEXT_CHARS = 20_000
_MAX_CHUNKS = 8
_MAX_CHUNK_CHARS = 2_500
_MAX_CHILDREN = 150
_MAX_TREE_DEPTH = 4


@dataclass
class ResourceRepresentation:
    kind: str
    summary: str = ""
    extracted_text: Optional[str] = None
    chunks: list[str] = field(default_factory=list)
    structured_data: dict[str, Any] = field(default_factory=dict)
    warnings: list[str] = field(default_factory=list)
    confidence: float = 1.0


@dataclass
class ResourceNode:
    id: str
    source_path: str
    display_name: str
    mime: str
    extension: str
    detected_type: str
    category: str
    size: int
    sha256: str
    metadata: dict[str, Any] = field(default_factory=dict)
    parent_id: Optional[str] = None
    children_ids: list[str] = field(default_factory=list)
    representations: list[ResourceRepresentation] = field(default_factory=list)
    status: str = "parsed"
    parse_error: Optional[str] = None
    relevance_score: float = 0.0


class AttachmentStore:
    def __init__(self, data_dir: Path) -> None:
        self.root = Path(data_dir) / "attachments"
        self.manifests = self.root / "derived"
        self.trees = self.root / "trees"
        self.previews = self.root / "previews"
        for d in (self.root, self.manifests, self.trees, self.previews):
            d.mkdir(parents=True, exist_ok=True)
        self._nodes: dict[str, ResourceNode] = {}

    def ingest_paths(self, paths: Iterable[str], query: str = "") -> dict[str, Any]:
        self._nodes = {}
        root_ids: list[str] = []
        for path_str in paths:
            p = Path(path_str)
            if not p.exists():
                log.warning("Attachment missing: %s", p)
                continue
            try:
                node = self._ingest_path(p, parent_id=None, depth=0)
                root_ids.append(node.id)
            except Exception as exc:
                log.exception("Attachment ingest failed: %s", p)
                root_ids.append(self._opaque_error_node(p, str(exc)).id)
        self._score_relevance(query)
        view = self._build_prompt_view(root_ids, query)
        self._persist(root_ids, view)
        return view

    def _opaque_error_node(self, p: Path, err: str) -> ResourceNode:
        node = ResourceNode(
            id=self._node_id(p, None), source_path=str(p), display_name=p.name,
            mime=mimetypes.guess_type(str(p))[0] or "application/octet-stream",
            extension=p.suffix.lower(), detected_type="opaque", category="unknown_binary",
            size=p.stat().st_size if p.exists() else 0, sha256=self._safe_hash(p),
            status="error", parse_error=err,
        )
        node.representations.append(ResourceRepresentation(
            kind="opaque", summary=f"Could not parse {p.name}: {err}",
            structured_data={"path": str(p), "error": err}, confidence=0.0,
        ))
        self._nodes[node.id] = node
        return node

    def _persist(self, root_ids: list[str], view: dict[str, Any]) -> None:
        manifest = {
            "root_ids": root_ids,
            "nodes": {nid: self._node_to_dict(n) for nid, n in self._nodes.items()},
            "view": view,
        }
        digest = hashlib.sha1("|".join(sorted(root_ids)).encode("utf-8")).hexdigest()[:16]
        (self.manifests / f"{digest}.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8"
        )

    def _node_to_dict(self, node: ResourceNode) -> dict[str, Any]:
        data = asdict(node)
        return data

    def _ingest_path(self, p: Path, parent_id: Optional[str], depth: int) -> ResourceNode:
        mime = mimetypes.guess_type(str(p))[0] or "application/octet-stream"
        ext = p.suffix.lower()
        detected_type, category = self._detect_type(p, mime)
        node = ResourceNode(
            id=self._node_id(p, parent_id),
            source_path=str(p),
            display_name=p.name,
            mime=mime,
            extension=ext,
            detected_type=detected_type,
            category=category,
            size=p.stat().st_size,
            sha256=self._safe_hash(p),
            parent_id=parent_id,
            metadata={"mtime": p.stat().st_mtime},
        )
        self._nodes[node.id] = node
        try:
            self._extract_node(node, p, depth)
        except Exception as exc:
            node.status = "error"
            node.parse_error = str(exc)
            node.representations.append(ResourceRepresentation(
                kind="opaque",
                summary=f"{p.name}: parse failed ({exc})",
                structured_data={"possible_role": self._infer_role_from_name(p.name)},
                warnings=[str(exc)],
                confidence=0.0,
            ))
        return node

    def _extract_node(self, node: ResourceNode, p: Path, depth: int) -> None:
        cat = node.category
        if cat == "image":
            node.representations.append(ResourceRepresentation(
                kind="image_asset",
                summary=f"Image asset {p.name} ({node.mime}, {node.size} bytes)",
                structured_data={"mime": node.mime, "size": node.size},
                confidence=1.0,
            ))
            return
        if cat in {"text", "code"}:
            txt = self._read_text_file(p)
            node.representations.append(self._text_representation(txt, p.name, cat))
            return
        if cat == "document":
            rep = self._extract_document(p)
            node.representations.append(rep)
            return
        if cat in {"spreadsheet", "tabular"}:
            node.representations.append(self._extract_spreadsheet(p))
            return
        if cat == "slides":
            node.representations.append(self._extract_slides(p))
            return
        if cat == "archive":
            self._expand_container(node, p, depth)
            node.representations.append(self._archive_manifest(node))
            return
        node.representations.append(self._unknown_representation(p, node))

    def _read_text_file(self, p: Path) -> str:
        data = p.read_bytes()
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            text = data.decode("utf-8", errors="replace")
        return text[:_MAX_TEXT_CHARS]

    def _text_representation(self, text: str, name: str, kind: str) -> ResourceRepresentation:
        summary = self._summarize_text(text, name)
        chunks = self._chunk_text(text)
        structured: dict[str, Any] = {}
        if kind == "code":
            structured = self._code_structure(text)
        return ResourceRepresentation(
            kind=kind, summary=summary, extracted_text=text, chunks=chunks,
            structured_data=structured, confidence=1.0,
        )

    def _extract_document(self, p: Path) -> ResourceRepresentation:
        ext = p.suffix.lower()
        if ext == ".pdf":
            text, warnings = self._pdf_text(p)
            return ResourceRepresentation(
                kind="document", summary=self._summarize_text(text, p.name),
                extracted_text=text, chunks=self._chunk_text(text),
                warnings=warnings, confidence=0.75 if warnings else 0.95,
            )
        if ext in {".docx", ".odt", ".epub"}:
            text = self._xml_container_text(p)
            return ResourceRepresentation(
                kind="document", summary=self._summarize_text(text, p.name),
                extracted_text=text, chunks=self._chunk_text(text),
                confidence=0.9,
            )
        if ext in {".html", ".htm", ".mhtml", ".rtf"}:
            text = self._read_text_file(p)
            return ResourceRepresentation(
                kind="document", summary=self._summarize_text(text, p.name),
                extracted_text=text, chunks=self._chunk_text(text), confidence=0.8,
            )
        return self._unknown_representation(p, None)

    def _extract_spreadsheet(self, p: Path) -> ResourceRepresentation:
        ext = p.suffix.lower()
        if ext in {".csv", ".tsv"}:
            delim = "," if ext == ".csv" else "\t"
            text = self._read_text_file(p)
            reader = csv.reader(io.StringIO(text), delimiter=delim)
            rows = []
            for i, row in enumerate(reader):
                rows.append(row[:20])
                if i >= 20:
                    break
            headers = rows[0] if rows else []
            summary = f"Tabular file {p.name}: {len(headers)} columns, sampled {max(0, len(rows)-1)} data rows"
            return ResourceRepresentation(
                kind="spreadsheet", summary=summary,
                extracted_text=text[:5000],
                chunks=[json.dumps(rows[:8], ensure_ascii=False)],
                structured_data={"headers": headers, "sample_rows": rows[1:8]},
                confidence=0.95,
            )
        if ext == ".xlsx":
            wb = self._xlsx_summary(p)
            return ResourceRepresentation(
                kind="spreadsheet",
                summary=wb["summary"],
                chunks=wb["chunks"],
                structured_data=wb,
                confidence=0.9,
            )
        return ResourceRepresentation(
            kind="spreadsheet",
            summary=f"Spreadsheet {p.name} detected but only metadata extracted",
            structured_data={"mime": mimetypes.guess_type(str(p))[0], "size": p.stat().st_size},
            warnings=["No parser available for this sheet format"],
            confidence=0.4,
        )

    def _extract_slides(self, p: Path) -> ResourceRepresentation:
        if p.suffix.lower() == ".pptx":
            slides = self._pptx_summary(p)
            return ResourceRepresentation(
                kind="slides",
                summary=slides["summary"],
                chunks=slides["chunks"],
                structured_data=slides,
                confidence=0.9,
            )
        return ResourceRepresentation(
            kind="slides",
            summary=f"Slide deck {p.name} detected but only metadata extracted",
            structured_data={"size": p.stat().st_size}, warnings=["No parser for this slide format"],
            confidence=0.4,
        )

    def _expand_container(self, node: ResourceNode, p: Path, depth: int) -> None:
        if depth >= _MAX_TREE_DEPTH:
            node.representations.append(ResourceRepresentation(
                kind="archive", summary=f"{p.name}: archive depth limit reached",
                warnings=["depth limit reached"], confidence=0.6,
            ))
            return
        children: list[Path] = []
        ext = p.suffix.lower()
        if zipfile.is_zipfile(p):
            with zipfile.ZipFile(p) as zf:
                infos = [i for i in zf.infolist() if not i.is_dir()][: _MAX_CHILDREN]
                node.metadata["archive_entries"] = len(infos)
                for info in infos:
                    if any(part in _SKIP_DIR_NAMES for part in Path(info.filename).parts):
                        continue
                    child = self._ingest_virtual_bytes(
                        info.filename, zf.read(info), parent_id=node.id, depth=depth + 1
                    )
                    node.children_ids.append(child.id)
            return
        if tarfile.is_tarfile(p):
            with tarfile.open(p) as tf:
                members = [m for m in tf.getmembers() if m.isfile()][: _MAX_CHILDREN]
                node.metadata["archive_entries"] = len(members)
                for member in members:
                    if any(part in _SKIP_DIR_NAMES for part in Path(member.name).parts):
                        continue
                    fh = tf.extractfile(member)
                    if fh is None:
                        continue
                    child = self._ingest_virtual_bytes(member.name, fh.read(), parent_id=node.id, depth=depth + 1)
                    node.children_ids.append(child.id)
            return
        if ext == ".gz" and not p.name.endswith(".tar.gz"):
            data = gzip.decompress(p.read_bytes())
            child_name = p.stem
            child = self._ingest_virtual_bytes(child_name, data, parent_id=node.id, depth=depth + 1)
            node.children_ids.append(child.id)
            return

    def _ingest_virtual_bytes(self, name: str, data: bytes, parent_id: str, depth: int) -> ResourceNode:
        tmp_name = name.rsplit("/", 1)[-1] or name
        mime = mimetypes.guess_type(tmp_name)[0] or "application/octet-stream"
        ext = Path(tmp_name).suffix.lower()
        detected_type, category = self._detect_virtual(tmp_name, data, mime)
        node = ResourceNode(
            id=self._node_id(Path(tmp_name), parent_id),
            source_path=f"virtual://{parent_id}/{name}",
            display_name=tmp_name,
            mime=mime,
            extension=ext,
            detected_type=detected_type,
            category=category,
            size=len(data),
            sha256=hashlib.sha256(data).hexdigest(),
            parent_id=parent_id,
            metadata={"virtual": True, "archive_name": name},
        )
        self._nodes[node.id] = node
        self._extract_virtual_node(node, tmp_name, data, depth)
        return node

    def _extract_virtual_node(self, node: ResourceNode, name: str, data: bytes, depth: int) -> None:
        cat = node.category
        if cat in {"text", "code", "document"}:
            text = self._decode_best_effort(data)[:_MAX_TEXT_CHARS]
            node.representations.append(self._text_representation(text, name, "code" if cat == "code" else "text"))
            return
        if cat in {"spreadsheet", "slides"}:
            node.representations.append(ResourceRepresentation(
                kind=cat, summary=f"{name}: detected inside archive; metadata only in virtual mode",
                structured_data={"size": len(data), "mime": node.mime}, confidence=0.4,
            ))
            return
        if cat == "archive" and depth < _MAX_TREE_DEPTH and zipfile.is_zipfile(io.BytesIO(data)):
            with zipfile.ZipFile(io.BytesIO(data)) as zf:
                infos = [i for i in zf.infolist() if not i.is_dir()][: _MAX_CHILDREN]
                for info in infos:
                    child = self._ingest_virtual_bytes(info.filename, zf.read(info), parent_id=node.id, depth=depth + 1)
                    node.children_ids.append(child.id)
            node.representations.append(self._archive_manifest(node))
            return
        node.representations.append(self._unknown_virtual_representation(name, data, node))

    def _archive_manifest(self, node: ResourceNode) -> ResourceRepresentation:
        children = [self._nodes[cid] for cid in node.children_ids if cid in self._nodes]
        lines = [f"- {c.display_name} [{c.category}] {self._fmt_size(c.size)}" for c in children[:20]]
        summary = f"Archive/container {node.display_name}: {len(children)} visible children"
        return ResourceRepresentation(
            kind="archive",
            summary=summary,
            chunks=["\n".join(lines)] if lines else [],
            structured_data={
                "child_count": len(children),
                "children": [{"name": c.display_name, "category": c.category, "size": c.size} for c in children[:40]],
            },
            confidence=0.9,
        )

    def _unknown_representation(self, p: Path, node: Optional[ResourceNode]) -> ResourceRepresentation:
        data = p.read_bytes()[:512]
        return self._unknown_virtual_representation(p.name, data, node)

    def _unknown_virtual_representation(self, name: str, data: bytes, node: Optional[ResourceNode]) -> ResourceRepresentation:
        ascii_preview = self._decode_best_effort(data)[:200]
        hex_preview = data[:32].hex(" ")
        role = self._infer_role_from_name(name)
        return ResourceRepresentation(
            kind="unknown",
            summary=f"Unknown/opaque file {name}; role inferred as {role}",
            structured_data={
                "ascii_preview": ascii_preview,
                "hex_preview": hex_preview,
                "possible_role": role,
                "entropy_hint": self._entropy_hint(data),
                "container_suspected": zipfile.is_zipfile(io.BytesIO(data)),
            },
            warnings=["No dedicated parser; metadata and preview only"],
            confidence=0.25,
        )

    def _build_prompt_view(self, root_ids: list[str], query: str) -> dict[str, Any]:
        selected = sorted(self._nodes.values(), key=lambda n: n.relevance_score, reverse=True)
        selected = [n for n in selected if n.relevance_score > 0][:16] or selected[:8]
        text_parts: list[str] = []
        image_paths: list[str] = []
        manifest_lines = ["Attachment manifest:"]
        for rid in root_ids:
            if rid in self._nodes:
                self._append_tree_line(self._nodes[rid], manifest_lines, depth=0)
        text_parts.append("\n".join(manifest_lines[:160]))

        for node in selected:
            if node.category == "image" and not node.source_path.startswith("virtual://"):
                image_paths.append(node.source_path)
                continue
            rep = node.representations[0] if node.representations else None
            if rep is None:
                continue
            block = [f"[FILE {node.display_name}] type={node.category} mime={node.mime} size={self._fmt_size(node.size)}"]
            if rep.summary:
                block.append(f"Summary: {rep.summary}")
            if rep.structured_data:
                try:
                    block.append("Structured: " + json.dumps(rep.structured_data, ensure_ascii=False)[:1200])
                except Exception:
                    pass
            payload = rep.extracted_text or "\n\n".join(rep.chunks[:2])
            if payload:
                block.append(payload[:3000])
            if rep.warnings:
                block.append("Warnings: " + "; ".join(rep.warnings[:3]))
            text_parts.append("\n".join(block))

        return {
            "query": query,
            "manifest": text_parts[0],
            "text_blocks": text_parts[1:],
            "image_paths": image_paths,
            "selected_ids": [n.id for n in selected],
        }

    def _append_tree_line(self, node: ResourceNode, out: list[str], depth: int) -> None:
        indent = "  " * depth
        out.append(f"{indent}- {node.display_name} [{node.category}] {self._fmt_size(node.size)}")
        if depth >= 2:
            return
        for cid in node.children_ids[:20]:
            child = self._nodes.get(cid)
            if child:
                self._append_tree_line(child, out, depth + 1)

    def _score_relevance(self, query: str) -> None:
        q_tokens = {t.lower() for t in query.replace("/", " ").replace("_", " ").split() if len(t) > 2}
        for node in self._nodes.values():
            score = 0.2
            name = node.display_name.lower()
            if node.category in {"code", "document", "spreadsheet", "slides"}:
                score += 1.0
            if node.category == "image":
                score += 0.4
            if any(k in name for k in ["readme", "main", "config", "requirements", "package", "manifest"]):
                score += 3.0
            if any(k in node.source_path.lower() for k in ["venv", "node_modules", "__pycache__", "/build/", "/dist/"]):
                score -= 5.0
            score += min(1.5, len(node.children_ids) * 0.1)
            if q_tokens:
                for tok in q_tokens:
                    if tok in name:
                        score += 1.2
                    if any(tok in (rep.summary or "").lower() for rep in node.representations[:1]):
                        score += 0.6
            node.relevance_score = score

    def _detect_type(self, p: Path, mime: str) -> tuple[str, str]:
        ext = p.suffix.lower()
        if ext in _IMAGE_EXTS or mime.startswith("image/"):
            return "image", "image"
        if ext in _ARCHIVE_EXTS or zipfile.is_zipfile(p) or tarfile.is_tarfile(p):
            return "archive", "archive"
        if ext in _DOC_EXTS:
            return "document", "document"
        if ext in _SHEET_EXTS:
            return "spreadsheet", "spreadsheet"
        if ext in _SLIDE_EXTS:
            return "slides", "slides"
        if ext in _TEXT_EXTS:
            return ("code", "code") if ext not in {".txt", ".md", ".rst", ".json", ".yaml", ".yml", ".csv", ".xml", ".html", ".htm", ".toml", ".ini", ".cfg", ".log"} else ("text", "text")
        if self._looks_textual(p):
            return "text", "text"
        return "unknown", "unknown_binary"

    def _detect_virtual(self, name: str, data: bytes, mime: str) -> tuple[str, str]:
        ext = Path(name).suffix.lower()
        if ext in _IMAGE_EXTS or mime.startswith("image/"):
            return "image", "image"
        if ext in _ARCHIVE_EXTS or zipfile.is_zipfile(io.BytesIO(data)):
            return "archive", "archive"
        if ext in _DOC_EXTS:
            return "document", "document"
        if ext in _SHEET_EXTS:
            return "spreadsheet", "spreadsheet"
        if ext in _SLIDE_EXTS:
            return "slides", "slides"
        if ext in _TEXT_EXTS:
            return ("code", "code") if ext not in {".txt", ".md", ".rst", ".json", ".yaml", ".yml", ".csv", ".xml", ".html", ".htm", ".toml", ".ini", ".cfg", ".log"} else ("text", "text")
        if self._bytes_look_textual(data):
            return "text", "text"
        return "unknown", "unknown_binary"

    def _looks_textual(self, p: Path) -> bool:
        return self._bytes_look_textual(p.read_bytes()[:512])

    def _bytes_look_textual(self, data: bytes) -> bool:
        if not data:
            return True
        if b"\x00" in data:
            return False
        textish = sum(32 <= b <= 126 or b in (9, 10, 13) for b in data)
        return textish / max(1, len(data)) > 0.8

    def _pdf_text(self, p: Path) -> tuple[str, list[str]]:
        warnings: list[str] = []
        try:
            from pypdf import PdfReader  # type: ignore
            reader = PdfReader(str(p))
            parts = []
            for page in reader.pages[:25]:
                parts.append(page.extract_text() or "")
            text = "\n\n".join(parts).strip()
            if not text:
                warnings.append("PDF had no extractable text")
                text = f"PDF {p.name}: no extractable text found"
            return text[:_MAX_TEXT_CHARS], warnings
        except Exception as exc:
            warnings.append(f"pypdf unavailable or failed: {exc}")
            return f"PDF {p.name}: text extraction unavailable", warnings

    def _xml_container_text(self, p: Path) -> str:
        texts: list[str] = []
        with zipfile.ZipFile(p) as zf:
            names = zf.namelist()
            preferred = [n for n in names if any(k in n for k in ["document.xml", "content.xml", "slides/slide", "chapter", "html"])]
            for name in preferred[:20]:
                try:
                    raw = zf.read(name)
                    txt = self._strip_xml_text(raw)
                    if txt:
                        texts.append(txt)
                except Exception:
                    continue
        if not texts:
            return f"{p.name}: container document detected, but text extraction returned nothing"
        return "\n\n".join(texts)[:_MAX_TEXT_CHARS]

    def _strip_xml_text(self, raw: bytes) -> str:
        try:
            root = ET.fromstring(raw)
            texts = [t.strip() for t in root.itertext() if t and t.strip()]
            return " ".join(texts)
        except Exception:
            return self._decode_best_effort(raw)

    def _xlsx_summary(self, p: Path) -> dict[str, Any]:
        summary: dict[str, Any] = {"sheets": [], "chunks": []}
        with zipfile.ZipFile(p) as zf:
            wb = ET.fromstring(zf.read("xl/workbook.xml"))
            ns = {"x": "http://schemas.openxmlformats.org/spreadsheetml/2006/main"}
            sheets = wb.findall(".//x:sheets/x:sheet", ns)
            shared_strings: list[str] = []
            if "xl/sharedStrings.xml" in zf.namelist():
                ss = ET.fromstring(zf.read("xl/sharedStrings.xml"))
                shared_strings = ["".join(t.itertext()) for t in ss.findall(".//{*}si")]
            for idx, sheet in enumerate(sheets[:8], start=1):
                name = sheet.attrib.get("name", f"Sheet{idx}")
                path = f"xl/worksheets/sheet{idx}.xml"
                if path not in zf.namelist():
                    continue
                root = ET.fromstring(zf.read(path))
                rows = []
                for row in root.findall(".//{*}sheetData/{*}row")[:12]:
                    vals = []
                    for c in row.findall("{*}c")[:12]:
                        t = c.attrib.get("t")
                        v = c.findtext("{*}v", default="")
                        if t == "s":
                            try:
                                vals.append(shared_strings[int(v)])
                            except Exception:
                                vals.append(v)
                        else:
                            vals.append(v)
                    rows.append(vals)
                headers = rows[0] if rows else []
                summary["sheets"].append({"name": name, "headers": headers, "sample_rows": rows[1:6]})
                summary["chunks"].append(json.dumps({"sheet": name, "rows": rows[:6]}, ensure_ascii=False))
        summary["summary"] = f"Workbook {p.name}: {len(summary['sheets'])} sheets sampled"
        return summary

    def _pptx_summary(self, p: Path) -> dict[str, Any]:
        result: dict[str, Any] = {"slides": [], "chunks": []}
        with zipfile.ZipFile(p) as zf:
            slide_names = sorted([n for n in zf.namelist() if n.startswith("ppt/slides/slide") and n.endswith(".xml")])
            for name in slide_names[:20]:
                raw = zf.read(name)
                txt = self._strip_xml_text(raw)
                first = txt[:500]
                slide_no = len(result["slides"]) + 1
                result["slides"].append({"slide": slide_no, "text": first})
                result["chunks"].append(f"Slide {slide_no}: {first}")
        result["summary"] = f"Slide deck {p.name}: {len(result['slides'])} slides sampled"
        return result

    def _summarize_text(self, text: str, name: str) -> str:
        stripped = " ".join(text.split())
        if not stripped:
            return f"{name}: empty or unreadable text"
        first = stripped[:260]
        lines = text.count("\n") + 1
        return f"{name}: {len(text)} chars, ~{lines} lines. Preview: {first}"

    def _chunk_text(self, text: str) -> list[str]:
        text = text[:_MAX_TEXT_CHARS]
        if len(text) <= _MAX_CHUNK_CHARS:
            return [text]
        chunks = []
        for i in range(0, len(text), _MAX_CHUNK_CHARS):
            chunks.append(text[i:i + _MAX_CHUNK_CHARS])
            if len(chunks) >= _MAX_CHUNKS:
                break
        return chunks

    def _code_structure(self, text: str) -> dict[str, Any]:
        lines = text.splitlines()
        imports = []
        symbols = []
        for line in lines[:300]:
            s = line.strip()
            if s.startswith(("import ", "from ", "#include ", "use ", "require(")):
                imports.append(s[:120])
            if s.startswith(("def ", "class ", "function ", "fn ", "struct ", "enum ")):
                symbols.append(s[:120])
        return {"imports": imports[:20], "symbols": symbols[:30], "line_count": len(lines)}

    def _safe_hash(self, p: Path) -> str:
        try:
            h = hashlib.sha256()
            with p.open("rb") as f:
                while True:
                    chunk = f.read(65536)
                    if not chunk:
                        break
                    h.update(chunk)
            return h.hexdigest()
        except Exception:
            return ""

    def _decode_best_effort(self, data: bytes) -> str:
        try:
            return data.decode("utf-8")
        except UnicodeDecodeError:
            return data.decode("utf-8", errors="replace")

    def _node_id(self, p: Path, parent_id: Optional[str]) -> str:
        seed = f"{parent_id or ''}|{p.name}|{str(p)}"
        return hashlib.sha1(seed.encode("utf-8", errors="replace")).hexdigest()[:16]

    def _fmt_size(self, size: int) -> str:
        for unit in ["B", "KB", "MB", "GB"]:
            if size < 1024 or unit == "GB":
                return f"{size:.0f}{unit}" if unit == "B" else f"{size:.1f}{unit}"
            size /= 1024
        return f"{size:.1f}GB"

    def _infer_role_from_name(self, name: str) -> str:
        low = name.lower()
        if any(k in low for k in ["model", "weights", ".bin", ".onnx", ".pt"]):
            return "model/artifact"
        if any(k in low for k in ["config", "settings", ".ini", ".toml", ".yaml", ".json"]):
            return "configuration"
        if any(k in low for k in ["readme", "manual", "guide", "spec"]):
            return "documentation"
        if any(k in low for k in ["main", "app", "server", "client"]):
            return "entrypoint/source"
        return "opaque asset"

    def _entropy_hint(self, data: bytes) -> str:
        if not data:
            return "empty"
        uniq = len(set(data)) / len(data)
        if uniq > 0.5:
            return "high"
        if uniq > 0.2:
            return "medium"
        return "low"
