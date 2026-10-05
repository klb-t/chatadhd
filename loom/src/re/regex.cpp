// OWNER: wave 2 semantic. Small backtracking regex engine with Python `re`
// semantics for the subset described in loom/re/regex.h.
//
// Architecture:
//   Parser    pattern (UTF-32) -> AST (Node tree)
//   Compiler  AST -> bytecode (vector<Inst>) for a small backtracking VM,
//             loosely modelled on Russ Cox's "sregex"/pike-vm style Split/Jmp
//             encoding, which reproduces sre's leftmost, greedy-first
//             backtracking order.
//   Exec      iterative VM: an explicit heap-allocated backtrack stack (not
//             the C++ call stack), so matching does not recurse per
//             character/repetition and is safe on small (Android) stacks.
//             Bounded by a step budget (Regex::set_step_limit).
#include "loom/re/regex.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>

#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom::re {

// ── AST ───────────────────────────────────────────────────────────────────
namespace {

enum class NodeKind {
  Char,
  Any,
  Class,
  Concat,
  Alt,
  Group,
  Repeat,   // generalised */+/?/{m,n} via rep_min/rep_max
  Bol,
  Eol,
  Bob,
  Eob,
  WordB,
  NWordB,
  Lookahead,
};

struct CharClassAst {
  bool negate = false;
  std::vector<std::pair<char32_t, char32_t>> ranges;
  bool has_w = false, has_W = false;
  bool has_d = false, has_D = false;
  bool has_s = false, has_S = false;

  bool raw_member(char32_t ch) const noexcept {
    for (const auto& r : ranges) {
      if (ch >= r.first && ch <= r.second) return true;
    }
    if (has_w && unicode::is_word(ch)) return true;
    if (has_d && unicode::is_decimal(ch)) return true;
    if (has_s && unicode::is_space(ch)) return true;
    if (has_W && !unicode::is_word(ch)) return true;
    if (has_D && !unicode::is_decimal(ch)) return true;
    if (has_S && !unicode::is_space(ch)) return true;
    return false;
  }

  bool matches(char32_t ch, bool ignorecase) const noexcept {
    bool m = raw_member(ch);
    if (!m && ignorecase) {
      char32_t lo = unicode::simple_lower(ch);
      char32_t up = unicode::simple_upper(ch);
      if (lo != ch && raw_member(lo)) m = true;
      else if (up != ch && raw_member(up)) m = true;
    }
    return m != negate;
  }
};

struct Node {
  NodeKind kind;
  char32_t ch = 0;                              // Char
  CharClassAst cls;                              // Class
  std::vector<std::unique_ptr<Node>> kids;        // Concat/Alt children; single child for Group/Repeat/Lookahead
  int group_index = -1;                           // Group: >=1 capturing, -1 non-capturing
  int rep_min = 0;
  int rep_max = -1;  // -1 = unbounded
  bool greedy = true;
  bool look_positive = true;  // Lookahead

  explicit Node(NodeKind k) : kind(k) {}
};

using NodePtr = std::unique_ptr<Node>;

NodePtr make(NodeKind k) { return std::make_unique<Node>(k); }

// ── Parser ───────────────────────────────────────────────────────────────

struct ParseError {
  std::string message;
  std::size_t offset;
};

class Parser {
 public:
  Parser(std::u32string_view pat, Flags initial_flags) : p_(pat), flags_(initial_flags) {}

  Result<NodePtr> parse(int& group_count, Flags& out_flags) {
    consume_leading_inline_flags();
    auto root = parse_alt();
    if (!root) return Error(Errc::Parse, err_->message);
    if (pos_ != p_.size()) {
      return Error(Errc::Parse, "unexpected '" + std::string(1, static_cast<char>(p_[pos_] < 128 ? p_[pos_] : '?')) +
                                     "' at offset " + std::to_string(pos_));
    }
    group_count = group_count_;
    out_flags = flags_;
    return std::move(*root);
  }

 private:
  std::u32string_view p_;
  std::size_t pos_ = 0;
  Flags flags_ = kNone;
  int group_count_ = 0;
  std::optional<ParseError> err_;

  bool eof() const noexcept { return pos_ >= p_.size(); }
  char32_t peek(std::size_t off = 0) const noexcept { return pos_ + off < p_.size() ? p_[pos_ + off] : U'\0'; }
  char32_t next() noexcept { return p_[pos_++]; }
  bool eat(char32_t c) noexcept {
    if (!eof() && peek() == c) {
      pos_++;
      return true;
    }
    return false;
  }

  void fail_at(std::string msg, std::size_t off) {
    if (!err_) err_ = ParseError{std::move(msg), off};
  }

  void consume_leading_inline_flags() {
    while (pos_ + 2 < p_.size() && p_[pos_] == U'(' && p_[pos_ + 1] == U'?') {
      std::size_t save = pos_;
      std::size_t i = pos_ + 2;
      Flags f = kNone;
      bool any = false;
      while (i < p_.size() && p_[i] != U')') {
        switch (p_[i]) {
          case U'i': f |= kIgnoreCase; any = true; break;
          case U'm': f |= kMultiline; any = true; break;
          case U's': f |= kDotAll; any = true; break;
          case U'u': any = true; break;
          default: any = false; i = p_.size() + 1;  // force failure of this attempt
        }
        if (i > p_.size()) break;
        i++;
      }
      if (any && i < p_.size() && p_[i] == U')') {
        flags_ |= f;
        pos_ = i + 1;
      } else {
        pos_ = save;
        break;
      }
    }
  }

  std::optional<NodePtr> parse_alt() {
    std::vector<NodePtr> branches;
    auto first = parse_concat();
    if (!first) return std::nullopt;
    branches.push_back(std::move(*first));
    while (eat(U'|')) {
      auto b = parse_concat();
      if (!b) return std::nullopt;
      branches.push_back(std::move(*b));
    }
    if (branches.size() == 1) return std::move(branches[0]);
    auto alt = make(NodeKind::Alt);
    alt->kids = std::move(branches);
    return alt;
  }

  std::optional<NodePtr> parse_concat() {
    auto cat = make(NodeKind::Concat);
    while (!eof() && peek() != U'|' && peek() != U')') {
      auto atom = parse_repeat();
      if (!atom) return std::nullopt;
      cat->kids.push_back(std::move(*atom));
    }
    return cat;
  }

  std::optional<NodePtr> parse_repeat() {
    auto atom = parse_atom();
    if (!atom) return std::nullopt;
    NodePtr node = std::move(*atom);
    for (;;) {
      if (eof()) break;
      char32_t c = peek();
      if (c == U'*' || c == U'+' || c == U'?') {
        pos_++;
        int mn = (c == U'+') ? 1 : 0;
        int mx = (c == U'?') ? 1 : -1;
        bool greedy = true;
        if (!eof() && peek() == U'?') {
          greedy = false;
          pos_++;
        }
        auto rep = make(NodeKind::Repeat);
        rep->rep_min = mn;
        rep->rep_max = mx;
        rep->greedy = greedy;
        rep->kids.push_back(std::move(node));
        node = std::move(rep);
        continue;
      }
      if (c == U'{') {
        std::size_t save = pos_;
        auto parsed = try_parse_braces();
        if (parsed) {
          auto [mn, mx] = *parsed;
          bool greedy = true;
          if (!eof() && peek() == U'?') {
            greedy = false;
            pos_++;
          }
          auto rep = make(NodeKind::Repeat);
          rep->rep_min = mn;
          rep->rep_max = mx;
          rep->greedy = greedy;
          rep->kids.push_back(std::move(node));
          node = std::move(rep);
          continue;
        }
        pos_ = save;  // '{' was not a valid quantifier -> literal, handled by next parse_atom() call
      }
      break;
    }
    return node;
  }

  // Parses "{m}" "{m,}" "{,n}" "{m,n}" starting at '{'. Returns nullopt
  // (without consuming) if the braces do not form a valid quantifier.
  std::optional<std::pair<int, int>> try_parse_braces() {
    std::size_t i = pos_;
    if (p_[i] != U'{') return std::nullopt;
    i++;
    std::size_t d1s = i;
    while (i < p_.size() && p_[i] >= U'0' && p_[i] <= U'9') i++;
    std::size_t d1e = i;
    bool has_comma = false;
    std::size_t d2s = 0, d2e = 0;
    if (i < p_.size() && p_[i] == U',') {
      has_comma = true;
      i++;
      d2s = i;
      while (i < p_.size() && p_[i] >= U'0' && p_[i] <= U'9') i++;
      d2e = i;
    }
    if (i >= p_.size() || p_[i] != U'}') return std::nullopt;
    if (d1s == d1e && (!has_comma || d2s == d2e)) return std::nullopt;  // "{}" or "{,}"
    auto to_int = [&](std::size_t s, std::size_t e) -> int {
      int v = 0;
      for (std::size_t k = s; k < e; ++k) v = v * 10 + static_cast<int>(p_[k] - U'0');
      return v;
    };
    int mn = (d1s == d1e) ? 0 : to_int(d1s, d1e);
    int mx;
    if (!has_comma) {
      mx = mn;
    } else if (d2s == d2e) {
      mx = -1;
    } else {
      mx = to_int(d2s, d2e);
    }
    pos_ = i + 1;
    if (mx >= 0 && mx < mn) return std::nullopt;
    return std::make_pair(mn, mx);
  }

  std::optional<NodePtr> parse_atom() {
    if (eof()) {
      fail_at("unexpected end of pattern", pos_);
      return std::nullopt;
    }
    char32_t c = next();
    switch (c) {
      case U'.': {
        return make(NodeKind::Any);
      }
      case U'^':
        return make(NodeKind::Bol);
      case U'$':
        return make(NodeKind::Eol);
      case U'(':
        return parse_group();
      case U'[':
        return parse_class();
      case U'\\':
        return parse_escape_atom();
      case U')':
        fail_at("unbalanced parenthesis", pos_ - 1);
        return std::nullopt;
      case U'*':
      case U'+':
      case U'?':
        fail_at("nothing to repeat", pos_ - 1);
        return std::nullopt;
      default: {
        auto n = make(NodeKind::Char);
        n->ch = c;
        return n;
      }
    }
  }

  std::optional<NodePtr> parse_group() {
    if (!eof() && peek() == U'?') {
      pos_++;
      if (eof()) {
        fail_at("unterminated group", pos_);
        return std::nullopt;
      }
      char32_t k = peek();
      if (k == U':') {
        pos_++;
        auto inner = parse_alt();
        if (!inner) return std::nullopt;
        if (!eat(U')')) {
          fail_at("missing closing parenthesis", pos_);
          return std::nullopt;
        }
        auto g = make(NodeKind::Group);
        g->group_index = -1;
        g->kids.push_back(std::move(*inner));
        return g;
      }
      if (k == U'=' || k == U'!') {
        pos_++;
        auto inner = parse_alt();
        if (!inner) return std::nullopt;
        if (!eat(U')')) {
          fail_at("missing closing parenthesis", pos_);
          return std::nullopt;
        }
        auto la = make(NodeKind::Lookahead);
        la->look_positive = (k == U'=');
        la->kids.push_back(std::move(*inner));
        return la;
      }
      // Inline flag group not at the very start, e.g. "a(?i)b": apply
      // globally (Loom only requires start-of-pattern placement, but we
      // accept it anywhere rather than fail).
      {
        std::size_t save = pos_ - 2;  // back to '('
        std::size_t i = pos_;
        Flags f = kNone;
        bool any = false;
        while (i < p_.size() && p_[i] != U')') {
          switch (p_[i]) {
            case U'i': f |= kIgnoreCase; any = true; break;
            case U'm': f |= kMultiline; any = true; break;
            case U's': f |= kDotAll; any = true; break;
            case U'u': any = true; break;
            default: any = false; i = p_.size() + 1;
          }
          if (i > p_.size()) break;
          i++;
        }
        if (any && i < p_.size() && p_[i] == U')') {
          flags_ |= f;
          pos_ = i + 1;
          auto empty = make(NodeKind::Concat);
          return empty;
        }
        pos_ = save;
      }
      fail_at("unsupported group syntax", pos_);
      return std::nullopt;
    }
    int idx = ++group_count_;
    auto inner = parse_alt();
    if (!inner) return std::nullopt;
    if (!eat(U')')) {
      fail_at("missing closing parenthesis", pos_);
      return std::nullopt;
    }
    auto g = make(NodeKind::Group);
    g->group_index = idx;
    g->kids.push_back(std::move(*inner));
    return g;
  }

  // Escape outside a character class.
  std::optional<NodePtr> parse_escape_atom() {
    if (eof()) {
      fail_at("bad escape (end of pattern)", pos_);
      return std::nullopt;
    }
    char32_t c = next();
    switch (c) {
      case U'b': return make(NodeKind::WordB);
      case U'B': return make(NodeKind::NWordB);
      case U'A': return make(NodeKind::Bob);
      case U'Z': return make(NodeKind::Eob);
      case U'w': case U'W': case U'd': case U'D': case U's': case U'S': {
        auto n = make(NodeKind::Class);
        set_shorthand(n->cls, c);
        return n;
      }
      case U'1': case U'2': case U'3': case U'4': case U'5':
      case U'6': case U'7': case U'8': case U'9':
        fail_at("backreferences are not supported", pos_ - 1);
        return std::nullopt;
      default: {
        auto lit = parse_escaped_literal(c);
        auto n = make(NodeKind::Char);
        n->ch = lit;
        return n;
      }
    }
  }

  static void set_shorthand(CharClassAst& cls, char32_t c) {
    switch (c) {
      case U'w': cls.has_w = true; break;
      case U'W': cls.has_W = true; break;
      case U'd': cls.has_d = true; break;
      case U'D': cls.has_D = true; break;
      case U's': cls.has_s = true; break;
      case U'S': cls.has_S = true; break;
      default: break;
    }
  }

  // `c` is the character right after the backslash (already consumed).
  // Resolves numeric escapes and known punctuation/control escapes to a
  // literal code point; unrecognised letters fall back to their literal
  // value (lenient).
  char32_t parse_escaped_literal(char32_t c) {
    switch (c) {
      case U't': return U'\t';
      case U'n': return U'\n';
      case U'r': return U'\r';
      case U'f': return U'\f';
      case U'v': return U'\v';
      case U'0': return U'\0';
      case U'x': {
        char32_t v = 0;
        for (int i = 0; i < 2 && !eof() && is_hex(peek()); ++i) v = v * 16 + hex_val(next());
        return v;
      }
      case U'u': {
        char32_t v = 0;
        for (int i = 0; i < 4 && !eof() && is_hex(peek()); ++i) v = v * 16 + hex_val(next());
        return v;
      }
      case U'U': {
        char32_t v = 0;
        for (int i = 0; i < 8 && !eof() && is_hex(peek()); ++i) v = v * 16 + hex_val(next());
        return v;
      }
      default:
        return c;  // \. \\ \( \) \[ \] \{ \} \| \* \+ \? \^ \$ \/ \- \' \" and any other -> literal
    }
  }

  static bool is_hex(char32_t c) noexcept {
    return (c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'f') || (c >= U'A' && c <= U'F');
  }
  static int hex_val(char32_t c) noexcept {
    if (c >= U'0' && c <= U'9') return static_cast<int>(c - U'0');
    if (c >= U'a' && c <= U'f') return static_cast<int>(c - U'a') + 10;
    return static_cast<int>(c - U'A') + 10;
  }

  std::optional<NodePtr> parse_class() {
    auto n = make(NodeKind::Class);
    if (!eof() && peek() == U'^') {
      n->cls.negate = true;
      pos_++;
    }
    bool first = true;
    for (;;) {
      if (eof()) {
        fail_at("unterminated character set", pos_);
        return std::nullopt;
      }
      if (peek() == U']' && !first) {
        pos_++;
        break;
      }
      first = false;
      if (peek() == U']') {  // literal ']' right after '[' or '[^'
        n->cls.ranges.push_back({U']', U']'});
        pos_++;
        continue;
      }
      ClassAtom lo = parse_class_atom(n->cls);
      if (!lo.is_literal) continue;  // shorthand consumed, not rangeable
      char32_t c1 = lo.ch;
      if (!eof() && peek() == U'-' && peek(1) != U']' && pos_ + 1 < p_.size()) {
        std::size_t save = pos_;
        pos_++;  // consume '-'
        ClassAtom hi = parse_class_atom(n->cls, /*for_range_end=*/true);
        if (hi.is_literal) {
          char32_t c2 = hi.ch;
          if (c2 < c1) {
            fail_at("bad character range", save);
            return std::nullopt;
          }
          n->cls.ranges.push_back({c1, c2});
          continue;
        }
        // Not a usable range end (e.g. shorthand class) -> '-' and c1 are literals.
        pos_ = save;
        n->cls.ranges.push_back({c1, c1});
        continue;
      }
      n->cls.ranges.push_back({c1, c1});
    }
    return n;
  }

  struct ClassAtom {
    bool is_literal;
    char32_t ch = 0;
  };

  ClassAtom parse_class_atom(CharClassAst& cls, bool for_range_end = false) {
    char32_t c = next();
    if (c != U'\\') return ClassAtom{true, c};
    if (eof()) return ClassAtom{true, U'\\'};
    char32_t e = next();
    switch (e) {
      case U'w': case U'W': case U'd': case U'D': case U's': case U'S':
        if (for_range_end) return ClassAtom{false, 0};
        set_shorthand(cls, e);
        return ClassAtom{false, 0};
      default:
        return ClassAtom{true, parse_escaped_literal(e)};
    }
  }
};

}  // namespace

// ── Bytecode ────────────────────────────────────────────────────────────
namespace {

enum class Op : std::uint8_t {
  Char, Any, Class, Match, Jmp, Split, Save,
  Bol, Eol, Bob, Eob, WordB, NWordB,
  Look, Fail,
};

struct Inst {
  Op op;
  char32_t ch = 0;
  int class_idx = -1;
  int x = -1, y = -1;
  int slot = -1;
  int look_idx = -1;
  bool look_positive = true;
};

struct SubProgram {
  std::vector<Inst> insts;
};

struct CompiledClasses {
  std::vector<CharClassAst> table;
};

}  // namespace

struct Regex::Program {
  explicit Program(RuntimeProfile recipe)
      : profile(std::move(recipe)), step_limit(profile.values().at("step_limit").get<std::uint64_t>()) {}

  RuntimeProfile profile;
  std::string pattern;
  Flags flags = kNone;
  int group_count = 0;
  std::size_t num_slots = 2;
  std::vector<Inst> insts;
  std::vector<CharClassAst> classes;
  std::vector<SubProgram> lookaheads;
  std::atomic<std::uint64_t> step_limit;
  mutable std::atomic<bool> hit_limit{false};
};

namespace {

// ── Compiler ────────────────────────────────────────────────────────────

class Compiler {
 public:
  explicit Compiler(Regex::Program& prog) : prog_(prog) {}

  void compile_root(const Node& root) {
    emit_save(0);
    compile(root, prog_.insts);
    emit_save(1);
    Inst m;
    m.op = Op::Match;
    prog_.insts.push_back(m);
  }

 private:
  Regex::Program& prog_;

  void emit_save(int slot) {
    Inst i;
    i.op = Op::Save;
    i.slot = slot;
    prog_.insts.push_back(i);
  }

  int add_class(const CharClassAst& c) {
    prog_.classes.push_back(c);
    return static_cast<int>(prog_.classes.size()) - 1;
  }

  void compile(const Node& n, std::vector<Inst>& out) {
    switch (n.kind) {
      case NodeKind::Char: {
        Inst i;
        i.op = Op::Char;
        i.ch = n.ch;
        out.push_back(i);
        break;
      }
      case NodeKind::Any: {
        Inst i;
        i.op = Op::Any;
        out.push_back(i);
        break;
      }
      case NodeKind::Class: {
        Inst i;
        i.op = Op::Class;
        i.class_idx = add_class(n.cls);
        out.push_back(i);
        break;
      }
      case NodeKind::Concat:
        for (const auto& k : n.kids) compile(*k, out);
        break;
      case NodeKind::Alt:
        compile_alt(n, out);
        break;
      case NodeKind::Group: {
        bool capturing = n.group_index > 0;
        if (capturing) {
          Inst s;
          s.op = Op::Save;
          s.slot = 2 * n.group_index;
          out.push_back(s);
        }
        compile(*n.kids[0], out);
        if (capturing) {
          Inst s;
          s.op = Op::Save;
          s.slot = 2 * n.group_index + 1;
          out.push_back(s);
        }
        break;
      }
      case NodeKind::Repeat:
        compile_repeat(n, out);
        break;
      case NodeKind::Bol: { Inst i; i.op = Op::Bol; out.push_back(i); break; }
      case NodeKind::Eol: { Inst i; i.op = Op::Eol; out.push_back(i); break; }
      case NodeKind::Bob: { Inst i; i.op = Op::Bob; out.push_back(i); break; }
      case NodeKind::Eob: { Inst i; i.op = Op::Eob; out.push_back(i); break; }
      case NodeKind::WordB: { Inst i; i.op = Op::WordB; out.push_back(i); break; }
      case NodeKind::NWordB: { Inst i; i.op = Op::NWordB; out.push_back(i); break; }
      case NodeKind::Lookahead: {
        SubProgram sp;
        Compiler sub(prog_);
        // Reuse the same class/lookahead tables (indices are global).
        sub.compile_into(*n.kids[0], sp.insts);
        prog_.lookaheads.push_back(std::move(sp));
        int idx = static_cast<int>(prog_.lookaheads.size()) - 1;
        Inst i;
        i.op = Op::Look;
        i.look_idx = idx;
        i.look_positive = n.look_positive;
        out.push_back(i);
        break;
      }
    }
  }

  // Compiles a standalone sub-pattern (for a lookahead body): body + Match.
  void compile_into(const Node& n, std::vector<Inst>& out) {
    compile(n, out);
    Inst m;
    m.op = Op::Match;
    out.push_back(m);
  }

  void compile_alt(const Node& n, std::vector<Inst>& out) { compile_alt_range(n.kids, 0, out); }

  // a|b|c  ==  split L1,Lrest ; L1: a ; jmp Lend ; Lrest: (b|c) ; Lend:
  void compile_alt_range(const std::vector<NodePtr>& kids, std::size_t idx, std::vector<Inst>& out) {
    if (idx + 1 == kids.size()) {
      compile(*kids[idx], out);
      return;
    }
    std::size_t split_pos = out.size();
    Inst split;
    split.op = Op::Split;
    out.push_back(split);
    std::size_t branch_start = out.size();
    compile(*kids[idx], out);
    std::size_t jmp_pos = out.size();
    Inst jmp;
    jmp.op = Op::Jmp;
    out.push_back(jmp);
    std::size_t rest_start = out.size();
    out[split_pos].x = static_cast<int>(branch_start);
    out[split_pos].y = static_cast<int>(rest_start);
    compile_alt_range(kids, idx + 1, out);
    out[jmp_pos].x = static_cast<int>(out.size());
  }

  void compile_quest(const Node& child, bool greedy, std::vector<Inst>& out) {
    std::size_t split_pos = out.size();
    Inst split;
    split.op = Op::Split;
    out.push_back(split);
    std::size_t body_start = out.size();
    compile(child, out);
    std::size_t after = out.size();
    if (greedy) {
      out[split_pos].x = static_cast<int>(body_start);
      out[split_pos].y = static_cast<int>(after);
    } else {
      out[split_pos].x = static_cast<int>(after);
      out[split_pos].y = static_cast<int>(body_start);
    }
  }

  void compile_star(const Node& child, bool greedy, std::vector<Inst>& out) {
    std::size_t l1 = out.size();
    Inst split;
    split.op = Op::Split;
    out.push_back(split);
    std::size_t body_start = out.size();
    compile(child, out);
    Inst jmp;
    jmp.op = Op::Jmp;
    jmp.x = static_cast<int>(l1);
    out.push_back(jmp);
    std::size_t after = out.size();
    if (greedy) {
      out[l1].x = static_cast<int>(body_start);
      out[l1].y = static_cast<int>(after);
    } else {
      out[l1].x = static_cast<int>(after);
      out[l1].y = static_cast<int>(body_start);
    }
  }

  void compile_plus(const Node& child, bool greedy, std::vector<Inst>& out) {
    std::size_t body_start = out.size();
    compile(child, out);
    std::size_t l1 = out.size();
    Inst split;
    split.op = Op::Split;
    out.push_back(split);
    std::size_t after = out.size();
    if (greedy) {
      out[l1].x = static_cast<int>(body_start);
      out[l1].y = static_cast<int>(after);
    } else {
      out[l1].x = static_cast<int>(after);
      out[l1].y = static_cast<int>(body_start);
    }
  }

  // (n-m) optional copies of `child`, NESTED (not sequential siblings): each
  // Quest's "skip" branch jumps past the whole remaining nest at once. This
  // is essential, not just style: sequential siblings E?E?E?...E? re-enter
  // every downstream copy fresh on each backtrack, which is O(2^k) for k
  // copies (the classic "a?a?a?...a?b" catastrophic-backtracking toy
  // example) — bounded quantifiers like {1,30} must stay O(k).
  void compile_nested_optional(const Node& child, int count, bool greedy, std::vector<Inst>& out) {
    if (count <= 0) return;
    std::size_t split_pos = out.size();
    Inst split;
    split.op = Op::Split;
    out.push_back(split);
    std::size_t body_start = out.size();
    compile(child, out);
    compile_nested_optional(child, count - 1, greedy, out);
    std::size_t after = out.size();
    if (greedy) {
      out[split_pos].x = static_cast<int>(body_start);
      out[split_pos].y = static_cast<int>(after);
    } else {
      out[split_pos].x = static_cast<int>(after);
      out[split_pos].y = static_cast<int>(body_start);
    }
  }

  void compile_repeat(const Node& n, std::vector<Inst>& out) {
    const Node& child = *n.kids[0];
    int mn = n.rep_min;
    int mx = n.rep_max;
    bool greedy = n.greedy;
    if (mn == 0 && mx == -1) { compile_star(child, greedy, out); return; }
    if (mn == 1 && mx == -1) { compile_plus(child, greedy, out); return; }
    if (mn == 0 && mx == 1) { compile_quest(child, greedy, out); return; }
    // General {m,n}: m mandatory copies, then either a star (n==-1) or
    // (n-m) nested optional copies.
    for (int i = 0; i < mn; ++i) compile(child, out);
    if (mx == -1) {
      compile_star(child, greedy, out);
    } else {
      compile_nested_optional(child, mx - mn, greedy, out);
    }
  }
};

// ── Exec ────────────────────────────────────────────────────────────────

struct ExecCtx {
  std::u32string_view text;
  const Regex::Program& prog;
  std::uint64_t steps = 0;
  std::uint64_t budget;
  bool hit_limit = false;

  explicit ExecCtx(std::u32string_view t, const Regex::Program& p)
      : text(t), prog(p), budget(p.step_limit.load()) {}

  bool ci() const noexcept { return prog.flags & kIgnoreCase; }
  bool multiline() const noexcept { return prog.flags & kMultiline; }
  bool dotall() const noexcept { return prog.flags & kDotAll; }

  bool char_eq(char32_t a, char32_t b) const noexcept {
    if (a == b) return true;
    if (!ci()) return false;
    return unicode::simple_lower(a) == unicode::simple_lower(b);
  }

  bool at_word_boundary(std::size_t sp) const noexcept {
    bool before = sp > 0 && unicode::is_word(text[sp - 1]);
    bool after = sp < text.size() && unicode::is_word(text[sp]);
    return before != after;
  }

  // Runs `insts` starting at fixed position `sp0`. `anchored_full`: require
  // consuming to end (fullmatch). Returns end-of-match sp on success (saves
  // filled in `saves`), or nullopt.
  std::optional<std::size_t> run(const std::vector<Inst>& insts, std::size_t sp0, bool require_end,
                                 std::vector<std::ptrdiff_t>& saves, std::optional<std::size_t>& lastindex) {
    struct UndoEntry {
      int slot;  // -1 => lastindex entry
      std::ptrdiff_t old_value;
    };
    struct Frame {
      int pc;
      std::size_t sp;
      std::size_t undo_mark;
    };
    std::vector<UndoEntry> undo;
    std::vector<Frame> stack;
    int pc = 0;
    std::size_t sp = sp0;
    std::ptrdiff_t li = lastindex ? static_cast<std::ptrdiff_t>(*lastindex) : -1;

    auto do_save = [&](int slot) {
      if (slot >= 3 && (slot % 2 == 1)) {
        undo.push_back({-1, li});
        li = (slot - 1) / 2;
      }
      undo.push_back({slot, saves[static_cast<std::size_t>(slot)]});
      saves[static_cast<std::size_t>(slot)] = static_cast<std::ptrdiff_t>(sp);
    };
    auto backtrack = [&]() -> bool {
      if (stack.empty()) return false;
      Frame f = stack.back();
      stack.pop_back();
      while (undo.size() > f.undo_mark) {
        UndoEntry e = undo.back();
        undo.pop_back();
        if (e.slot == -1) li = e.old_value;
        else saves[static_cast<std::size_t>(e.slot)] = e.old_value;
      }
      pc = f.pc;
      sp = f.sp;
      return true;
    };

    for (;;) {
      if (++steps > budget) {
        hit_limit = true;
        return std::nullopt;
      }
      const Inst& in = insts[static_cast<std::size_t>(pc)];
      bool ok = true;
      switch (in.op) {
        case Op::Char:
          if (sp < text.size() && char_eq(text[sp], in.ch)) { sp++; pc++; }
          else ok = false;
          break;
        case Op::Any:
          if (sp < text.size() && (dotall() || text[sp] != U'\n')) { sp++; pc++; }
          else ok = false;
          break;
        case Op::Class:
          if (sp < text.size() && prog.classes[static_cast<std::size_t>(in.class_idx)].matches(text[sp], ci())) {
            sp++;
            pc++;
          } else ok = false;
          break;
        case Op::Bol:
          if (sp == 0 || (multiline() && text[sp - 1] == U'\n')) pc++;
          else ok = false;
          break;
        case Op::Eol: {
          bool eol_ok = false;
          if (sp == text.size()) eol_ok = true;
          else if (multiline()) eol_ok = (text[sp] == U'\n');
          else eol_ok = (sp + 1 == text.size() && text[sp] == U'\n');
          if (eol_ok) pc++;
          else ok = false;
          break;
        }
        case Op::Bob:
          if (sp == 0) pc++;
          else ok = false;
          break;
        case Op::Eob:
          if (sp == text.size()) pc++;
          else ok = false;
          break;
        case Op::WordB:
          if (at_word_boundary(sp)) pc++;
          else ok = false;
          break;
        case Op::NWordB:
          if (!at_word_boundary(sp)) pc++;
          else ok = false;
          break;
        case Op::Save:
          do_save(in.slot);
          pc++;
          break;
        case Op::Jmp:
          pc = in.x;
          break;
        case Op::Split:
          stack.push_back({in.y, sp, undo.size()});
          pc = in.x;
          break;
        case Op::Look: {
          const SubProgram& sub = prog.lookaheads[static_cast<std::size_t>(in.look_idx)];
          std::size_t mark = undo.size();
          std::optional<std::size_t> lastindex_dummy = lastindex;
          bool matched = run(sub.insts, sp, false, saves, lastindex_dummy).has_value();
          if (matched != in.look_positive) {
            // Fail this path; undo anything the (failed-for-our-purposes)
            // sub-match may have written.
            while (undo.size() > mark) {
              UndoEntry e = undo.back();
              undo.pop_back();
              if (e.slot == -1) li = e.old_value;
              else saves[static_cast<std::size_t>(e.slot)] = e.old_value;
            }
            ok = false;
          } else {
            if (matched && in.look_positive) {
              lastindex = lastindex_dummy;
              li = lastindex_dummy ? static_cast<std::ptrdiff_t>(*lastindex_dummy) : li;
            } else {
              while (undo.size() > mark) {
                UndoEntry e = undo.back();
                undo.pop_back();
                if (e.slot == -1) li = e.old_value;
                else saves[static_cast<std::size_t>(e.slot)] = e.old_value;
              }
            }
            pc++;
          }
          break;
        }
        case Op::Match:
          if (require_end && sp != text.size()) { ok = false; break; }
          lastindex = (li >= 0) ? std::optional<std::size_t>(static_cast<std::size_t>(li)) : std::nullopt;
          return sp;
        case Op::Fail:
          ok = false;
          break;
      }
      if (!ok) {
        if (!backtrack()) return std::nullopt;
      }
    }
  }
};

}  // namespace

std::u32string_view Match::group(std::size_t g) const noexcept {
  Span s = span(g);
  if (!s.matched() || static_cast<std::size_t>(s.end) > subject_.size()) return {};
  return subject_.substr(static_cast<std::size_t>(s.start), static_cast<std::size_t>(s.end - s.start));
}

std::string Match::group_utf8(std::size_t g) const { return utf8::encode(group(g)); }

Regex::~Regex() = default;

Result<Regex> Regex::compile(std::string_view pattern_utf8, Flags flags) {
  LOOM_TRY_ASSIGN(auto profile, RuntimeProfile::builtin("re"));
  return compile(pattern_utf8, flags, profile);
}

namespace {
Result<RuntimeProfile> validate_regex_profile(const RuntimeProfile& profile) {
  if (profile.domain() != "re")
    return Error(Errc::InvalidArgument, "regex requires the re runtime profile");
  LOOM_TRY_ASSIGN(auto consumer, RuntimeProfile::builtin("re"));
  // Caller-defined schemas cannot weaken this consumer's representation
  // requirements or advertise executable settings the VM does not read.
  LOOM_TRY(consumer.with_values(profile.values()));
  return profile;
}
}  // namespace

Result<Regex> Regex::compile(std::string_view pattern_utf8, Flags flags, const RuntimeProfile& profile) {
  LOOM_TRY_ASSIGN(auto recipe, validate_regex_profile(profile));
  std::u32string pat32 = utf8::decode(pattern_utf8);
  auto prog = std::make_shared<Program>(std::move(recipe));
  prog->pattern = std::string(pattern_utf8);

  Parser parser(pat32, flags);
  int group_count = 0;
  Flags out_flags = flags;
  auto ast = parser.parse(group_count, out_flags);
  if (!ast) return Error(ast.error());

  prog->flags = out_flags;
  prog->group_count = group_count;
  prog->num_slots = 2 * static_cast<std::size_t>(group_count + 1);

  Compiler compiler(*prog);
  compiler.compile_root(**ast);

  return Regex(prog);
}

Result<Regex> Regex::with_profile(const RuntimeProfile& profile) const {
  if (!prog_) return Error(Errc::Unavailable, "regex has no compiled program");
  LOOM_TRY_ASSIGN(auto recipe, validate_regex_profile(profile));
  auto program = std::make_shared<Program>(std::move(recipe));
  program->pattern = prog_->pattern;
  program->flags = prog_->flags;
  program->group_count = prog_->group_count;
  program->num_slots = prog_->num_slots;
  program->insts = prog_->insts;
  program->classes = prog_->classes;
  program->lookaheads = prog_->lookaheads;
  return Regex(std::move(program));
}

Result<Json> Regex::profile_inspection() const {
  if (!prog_) return Error(Errc::Unavailable, "regex has no compiled program");
  LOOM_TRY_ASSIGN(auto effective, prog_->profile.with_overrides(Json{{"step_limit", step_limit()}}));
  return effective.inspection();
}

namespace {
std::optional<Match> build_match(std::u32string_view text, const Regex::Program& prog,
                                 const std::vector<std::ptrdiff_t>& saves, std::optional<std::size_t> lastindex) {
  std::vector<Span> spans;
  spans.reserve(prog.num_slots / 2);
  for (std::size_t g = 0; g * 2 < prog.num_slots; ++g) {
    std::ptrdiff_t s = saves[g * 2];
    std::ptrdiff_t e = saves[g * 2 + 1];
    if (s < 0 || e < 0) spans.push_back(Span{-1, -1});
    else spans.push_back(Span{s, e});
  }
  return Match(text, std::move(spans), lastindex);
}
}  // namespace

std::optional<Match> Regex::search(std::u32string_view text, std::size_t pos) const {
  if (!prog_) return std::nullopt;
  prog_->hit_limit.store(false);
  ExecCtx ctx(text, *prog_);
  for (std::size_t start = pos; start <= text.size(); ++start) {
    std::vector<std::ptrdiff_t> saves(prog_->num_slots, -1);
    std::optional<std::size_t> lastindex;
    auto end = ctx.run(prog_->insts, start, false, saves, lastindex);
    if (ctx.hit_limit) {
      prog_->hit_limit.store(true);
      return std::nullopt;
    }
    if (end) return build_match(text, *prog_, saves, lastindex);
  }
  return std::nullopt;
}

std::optional<Match> Regex::match(std::u32string_view text, std::size_t pos) const {
  if (!prog_ || pos > text.size()) return std::nullopt;
  prog_->hit_limit.store(false);
  ExecCtx ctx(text, *prog_);
  std::vector<std::ptrdiff_t> saves(prog_->num_slots, -1);
  std::optional<std::size_t> lastindex;
  auto end = ctx.run(prog_->insts, pos, false, saves, lastindex);
  if (ctx.hit_limit) {
    prog_->hit_limit.store(true);
    return std::nullopt;
  }
  if (!end) return std::nullopt;
  return build_match(text, *prog_, saves, lastindex);
}

std::optional<Match> Regex::fullmatch(std::u32string_view text, std::size_t pos) const {
  if (!prog_ || pos > text.size()) return std::nullopt;
  prog_->hit_limit.store(false);
  ExecCtx ctx(text, *prog_);
  std::vector<std::ptrdiff_t> saves(prog_->num_slots, -1);
  std::optional<std::size_t> lastindex;
  auto end = ctx.run(prog_->insts, pos, true, saves, lastindex);
  if (ctx.hit_limit) {
    prog_->hit_limit.store(true);
    return std::nullopt;
  }
  if (!end) return std::nullopt;
  return build_match(text, *prog_, saves, lastindex);
}

std::vector<Match> Regex::finditer(std::u32string_view text) const {
  std::vector<Match> out;
  std::size_t pos = 0;
  while (pos <= text.size()) {
    auto m = search(text, pos);
    if (!m) break;
    std::size_t mstart = static_cast<std::size_t>(m->start(0));
    std::size_t mend = static_cast<std::size_t>(m->end(0));
    out.push_back(*m);
    pos = (mend == mstart) ? mend + 1 : mend;
  }
  return out;
}

std::vector<std::u32string> Regex::findall(std::u32string_view text) const {
  std::vector<std::u32string> out;
  std::size_t g = group_count() >= 1 ? 1 : 0;
  for (const auto& m : finditer(text)) out.emplace_back(m.group(g));
  return out;
}

namespace {
// Parses a Python-style replacement template into literal runs / group refs.
struct ReplPart {
  bool is_group;
  std::u32string lit;
  int group = 0;
};
std::vector<ReplPart> parse_repl(std::u32string_view repl) {
  std::vector<ReplPart> parts;
  std::u32string cur;
  std::size_t i = 0;
  auto flush = [&] {
    if (!cur.empty()) {
      parts.push_back(ReplPart{false, cur, 0});
      cur.clear();
    }
  };
  while (i < repl.size()) {
    char32_t c = repl[i];
    if (c != U'\\' || i + 1 >= repl.size()) {
      cur.push_back(c);
      i++;
      continue;
    }
    char32_t e = repl[i + 1];
    if (e >= U'0' && e <= U'9') {
      std::size_t j = i + 1;
      int v = 0;
      int digits = 0;
      while (j < repl.size() && repl[j] >= U'0' && repl[j] <= U'9' && digits < 2) {
        v = v * 10 + static_cast<int>(repl[j] - U'0');
        j++;
        digits++;
      }
      flush();
      parts.push_back(ReplPart{true, U"", v});
      i = j;
      continue;
    }
    if (e == U'g' && i + 2 < repl.size() && repl[i + 2] == U'<') {
      std::size_t j = i + 3;
      int v = 0;
      bool any = false;
      while (j < repl.size() && repl[j] >= U'0' && repl[j] <= U'9') {
        v = v * 10 + static_cast<int>(repl[j] - U'0');
        j++;
        any = true;
      }
      if (any && j < repl.size() && repl[j] == U'>') {
        flush();
        parts.push_back(ReplPart{true, U"", v});
        i = j + 1;
        continue;
      }
    }
    switch (e) {
      case U'n': cur.push_back(U'\n'); i += 2; break;
      case U't': cur.push_back(U'\t'); i += 2; break;
      case U'r': cur.push_back(U'\r'); i += 2; break;
      case U'f': cur.push_back(U'\f'); i += 2; break;
      case U'v': cur.push_back(U'\v'); i += 2; break;
      case U'\\': cur.push_back(U'\\'); i += 2; break;
      default: cur.push_back(e); i += 2; break;
    }
  }
  flush();
  return parts;
}
}  // namespace

std::u32string Regex::sub(std::u32string_view repl, std::u32string_view text, std::size_t count) const {
  auto parts = parse_repl(repl);
  std::u32string out;
  std::size_t pos = 0;
  std::size_t done = 0;
  std::size_t last_copied = 0;
  while (pos <= text.size() && (count == 0 || done < count)) {
    auto m = search(text, pos);
    if (!m) break;
    std::size_t mstart = static_cast<std::size_t>(m->start(0));
    std::size_t mend = static_cast<std::size_t>(m->end(0));
    out.append(text.substr(last_copied, mstart - last_copied));
    for (const auto& part : parts) {
      if (!part.is_group) {
        out.append(part.lit);
      } else {
        auto g = m->group(static_cast<std::size_t>(part.group));
        out.append(g);
      }
    }
    last_copied = mend;
    done++;
    pos = (mend == mstart) ? mend + 1 : mend;
  }
  out.append(text.substr(last_copied));
  return out;
}

std::string Regex::sub_utf8(std::string_view repl, std::string_view text, std::size_t count) const {
  return utf8::encode(sub(utf8::decode(repl), utf8::decode(text), count));
}

bool Regex::search_utf8(std::string_view text) const { return search(utf8::decode(text)).has_value(); }

std::size_t Regex::group_count() const noexcept { return prog_ ? static_cast<std::size_t>(prog_->group_count) : 0; }
const std::string& Regex::pattern() const noexcept {
  static const std::string kEmpty;
  return prog_ ? prog_->pattern : kEmpty;
}
Flags Regex::flags() const noexcept { return prog_ ? prog_->flags : kNone; }
void Regex::set_step_limit(std::uint64_t steps) noexcept {
  if (prog_) prog_->step_limit.store(steps);
}
std::uint64_t Regex::step_limit() const noexcept { return prog_ ? prog_->step_limit.load() : 0; }
bool Regex::last_search_hit_limit() const noexcept { return prog_ && prog_->hit_limit.load(); }

}  // namespace loom::re
