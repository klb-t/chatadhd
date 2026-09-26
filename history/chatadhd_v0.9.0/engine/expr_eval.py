"""
engine/expr_eval.py
====================

Mały bezpieczny evaluator wyrażeń matematycznych dla camera paths, zoom curves,
animation. Zmienna wejściowa: `t` (czas w sekundach).

Projekt:
  - Żadnego eval/exec na raw stringu.
  - Parser: ast.parse w trybie 'eval', whitelist node types, whitelist nazw.
  - Primitives: +, -, *, /, **, %, //, sin, cos, tan, exp, log, sqrt, abs,
    min, max, pi, e, clamp, lerp, smoothstep, floor, ceil, round.
  - Zmienna: tylko 't'.
  - Bezpieczny na untrusted input (LLM generuje te wyrażenia).

Przykłady:
  evaluate("2.0 + 0.5 * sin(t * 0.5)", t=1.5)
  evaluate("lerp(0, 10, clamp(t/4, 0, 1))", t=2.0)
"""

from __future__ import annotations

import ast
import logging
import math
from typing import Any, Dict, List, Tuple, Union

log = logging.getLogger(__name__)


# ─── Whitelist ──────────────────────────────────────────────────

def _clamp(x: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, x))


def _lerp(a: float, b: float, u: float) -> float:
    return a + (b - a) * u


def _smoothstep(edge0: float, edge1: float, x: float) -> float:
    if edge1 == edge0:
        return 0.0 if x < edge0 else 1.0
    t = _clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def _sign(x: float) -> float:
    return float((x > 0) - (x < 0))


_ALLOWED_NAMES: Dict[str, Any] = {
    "pi": math.pi,
    "e": math.e,
    "tau": math.tau,
    "sin": math.sin, "cos": math.cos, "tan": math.tan,
    "asin": math.asin, "acos": math.acos, "atan": math.atan,
    "atan2": math.atan2,
    "exp": math.exp, "log": math.log, "log2": math.log2, "log10": math.log10,
    "sqrt": math.sqrt, "abs": abs,
    "min": min, "max": max,
    "floor": math.floor, "ceil": math.ceil, "round": round,
    "pow": pow,
    "sinh": math.sinh, "cosh": math.cosh, "tanh": math.tanh,
    "clamp": _clamp, "lerp": _lerp, "smoothstep": _smoothstep, "sign": _sign,
}


_ALLOWED_NODES = (
    ast.Expression,
    ast.Constant, ast.Num,
    ast.Name, ast.Load,
    ast.BinOp, ast.UnaryOp, ast.Call,
    ast.Add, ast.Sub, ast.Mult, ast.Div, ast.Mod, ast.Pow, ast.FloorDiv,
    ast.USub, ast.UAdd,
    ast.Tuple, ast.List,
    ast.Compare, ast.Gt, ast.Lt, ast.GtE, ast.LtE, ast.Eq, ast.NotEq,
    ast.IfExp,
    ast.BoolOp, ast.And, ast.Or,
)


class ExpressionError(Exception):
    pass


def compile_expr(expr: str) -> ast.Expression:
    """Parsuje i waliduje expression. Raises ExpressionError dla disallowed constructs."""
    try:
        tree = ast.parse(expr, mode="eval")
    except SyntaxError as e:
        raise ExpressionError(f"Parse error in '{expr}': {e}")

    for node in ast.walk(tree):
        if not isinstance(node, _ALLOWED_NODES):
            raise ExpressionError(
                f"Disallowed construct {type(node).__name__} in '{expr}'"
            )
        if isinstance(node, ast.Name):
            if node.id != "t" and node.id not in _ALLOWED_NAMES:
                raise ExpressionError(f"Unknown name '{node.id}' in '{expr}'")
        if isinstance(node, ast.Call):
            if not isinstance(node.func, ast.Name):
                raise ExpressionError(
                    f"Only direct function calls allowed in '{expr}'"
                )
            if node.func.id not in _ALLOWED_NAMES:
                raise ExpressionError(
                    f"Disallowed function '{node.func.id}' in '{expr}'"
                )
    return tree


def evaluate_tree(tree: ast.Expression, t: float = 0.0) -> float:
    """Ewaluuje skompilowane drzewo dla danego t."""
    namespace = dict(_ALLOWED_NAMES)
    namespace["t"] = float(t)
    try:
        code = compile(tree, filename="<expr>", mode="eval")
        return float(eval(code, {"__builtins__": {}}, namespace))  # noqa: S307
    except Exception as e:
        raise ExpressionError(f"Eval failed at t={t}: {e}")


def evaluate(expr: str, t: float = 0.0) -> float:
    """One-shot: parse + eval."""
    tree = compile_expr(expr)
    return evaluate_tree(tree, t)


def sample(expr: str, t_values: List[float]) -> List[float]:
    """Wiele ewaluacji jednego wyrażenia."""
    tree = compile_expr(expr)
    return [evaluate_tree(tree, t) for t in t_values]


# ─── Curve utilities ────────────────────────────────────────────

def evaluate_scalar_curve(curve: Any, t: float) -> float:
    """
    Ewaluuje scalar_curve zgodny z JSON Schema. Obsługuje:
      - number (shorthand stałej)
      - {"type": "expression", "expression": "...", "fallback_samples": [...]}
      - {"type": "piecewise", "control_points": [[t, v], ...], "interpolation": "..."}
    """
    if isinstance(curve, (int, float)):
        return float(curve)
    if not isinstance(curve, dict):
        log.warning("evaluate_scalar_curve: unexpected type %s", type(curve))
        return 0.0

    curve_type = curve.get("type")

    if curve_type == "expression":
        expr = curve.get("expression", "0")
        try:
            return evaluate(expr, t=t)
        except ExpressionError:
            log.exception("Expression eval failed; trying fallback samples")
            samples = curve.get("fallback_samples") or []
            if samples:
                return _interpolate_piecewise(
                    [(s[0], s[1]) for s in samples], t, "linear"
                )
            return 0.0

    if curve_type == "piecewise":
        pts = [(p[0], p[1]) for p in curve.get("control_points", [])]
        interp = curve.get("interpolation", "linear")
        return _interpolate_piecewise(pts, t, interp)

    log.warning("Unknown scalar_curve type: %s", curve_type)
    return 0.0


def _interpolate_piecewise(points: List[Tuple[float, float]], t: float,
                            mode: str) -> float:
    if not points:
        return 0.0
    if len(points) == 1:
        return float(points[0][1])
    points = sorted(points, key=lambda p: p[0])
    if t <= points[0][0]:
        return float(points[0][1])
    if t >= points[-1][0]:
        return float(points[-1][1])
    for i in range(len(points) - 1):
        t0, v0 = points[i]
        t1, v1 = points[i + 1]
        if t0 <= t <= t1:
            u = (t - t0) / (t1 - t0) if t1 != t0 else 0.0
            if mode == "step":
                return float(v0)
            if mode == "cosine":
                u2 = (1 - math.cos(u * math.pi)) / 2
                return float(v0 + (v1 - v0) * u2)
            if mode == "bezier":
                u2 = u * u * (3 - 2 * u)
                return float(v0 + (v1 - v0) * u2)
            return float(v0 + (v1 - v0) * u)
    return float(points[-1][1])


def evaluate_vec3_curve(curve: Any, t: float) -> Tuple[float, float, float]:
    """Ewaluuje vec3_curve. Zwraca (x, y, z)."""
    if isinstance(curve, (list, tuple)) and len(curve) == 3:
        return (float(curve[0]), float(curve[1]), float(curve[2]))
    if not isinstance(curve, dict):
        return (0.0, 0.0, 0.0)

    curve_type = curve.get("type")

    if curve_type == "expression_per_axis":
        try:
            return (
                evaluate(curve.get("x", "0"), t=t),
                evaluate(curve.get("y", "0"), t=t),
                evaluate(curve.get("z", "0"), t=t),
            )
        except ExpressionError:
            log.exception("vec3 expr eval failed; trying fallback samples")
            samples = curve.get("fallback_samples") or []
            if samples:
                return _interpolate_vec3_piecewise(
                    [(s[0], s[1], s[2], s[3]) for s in samples], t, "linear"
                )
            return (0.0, 0.0, 0.0)

    if curve_type == "piecewise":
        pts = [(p[0], p[1], p[2], p[3])
               for p in curve.get("control_points", [])]
        interp = curve.get("interpolation", "linear")
        return _interpolate_vec3_piecewise(pts, t, interp)

    return (0.0, 0.0, 0.0)


def _interpolate_vec3_piecewise(points: List[Tuple[float, float, float, float]],
                                  t: float, mode: str) -> Tuple[float, float, float]:
    if not points:
        return (0.0, 0.0, 0.0)
    if len(points) == 1:
        p = points[0]
        return (float(p[1]), float(p[2]), float(p[3]))
    points = sorted(points, key=lambda p: p[0])
    if t <= points[0][0]:
        p = points[0]
        return (float(p[1]), float(p[2]), float(p[3]))
    if t >= points[-1][0]:
        p = points[-1]
        return (float(p[1]), float(p[2]), float(p[3]))
    for i in range(len(points) - 1):
        t0, x0, y0, z0 = points[i]
        t1, x1, y1, z1 = points[i + 1]
        if t0 <= t <= t1:
            u = (t - t0) / (t1 - t0) if t1 != t0 else 0.0
            if mode == "step":
                return (float(x0), float(y0), float(z0))
            if mode == "cosine":
                u2 = (1 - math.cos(u * math.pi)) / 2
                return (
                    float(x0 + (x1 - x0) * u2),
                    float(y0 + (y1 - y0) * u2),
                    float(z0 + (z1 - z0) * u2),
                )
            if mode == "bezier":
                u2 = u * u * (3 - 2 * u)
                return (
                    float(x0 + (x1 - x0) * u2),
                    float(y0 + (y1 - y0) * u2),
                    float(z0 + (z1 - z0) * u2),
                )
            return (
                float(x0 + (x1 - x0) * u),
                float(y0 + (y1 - y0) * u),
                float(z0 + (z1 - z0) * u),
            )
    p = points[-1]
    return (float(p[1]), float(p[2]), float(p[3]))
