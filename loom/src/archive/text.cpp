// Text utilities for the archive pipeline: tokenisation, stop words (PL+EN),
// sentence splitting, dates, identifiers. Pure functions.
#include <cctype>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <unordered_map>
#include <unordered_set>

#include "archive/archive_internal.h"
#include "loom/util/sha256.h"
#include "loom/util/unicode.h"
#include "loom/util/utf8.h"

namespace loom::archive {
namespace {

bool all_digits(std::u32string_view s) {
  for (char32_t c : s) {
    if (!unicode::is_decimal(c) && !unicode::is_digit(c)) return false;
  }
  return !s.empty();
}

const std::unordered_set<std::string>& stopwords() {
  static const std::unordered_set<std::string> kSet = [] {
    // English + Polish function words, discourse filler, and code keywords
    // that carry no topical signal. Policy data.
    const char* words[] = {
        // English
        "a", "about", "above", "after", "again", "against", "all", "also", "am", "an", "and", "any", "are",
        "aren", "as", "at", "be", "because", "been", "before", "being", "below", "between", "both", "but", "by",
        "can", "cannot", "could", "did", "didn", "do", "does", "doesn", "doing", "don", "down", "during", "each",
        "else", "etc", "even", "ever", "every", "few", "for", "from", "further", "get", "gets", "got", "had",
        "has", "have", "having", "he", "her", "here", "hers", "him", "his", "how", "however", "i", "if", "in",
        "into", "is", "isn", "it", "its", "itself", "just", "let", "like", "ll", "make", "makes", "many", "may",
        "me", "might", "more", "most", "much", "my", "no", "nor", "not", "now", "of", "off", "on", "once", "one",
        "only", "or", "other", "our", "ours", "out", "over", "own", "per", "same", "she", "should", "so", "some",
        "such", "than", "that", "the", "their", "theirs", "them", "then", "there", "these", "they", "this",
        "those", "through", "to", "too", "under", "until", "up", "upon", "us", "use", "used", "uses", "using",
        "very", "via", "was", "wasn", "we", "were", "what", "when", "where", "whether", "which", "while", "who",
        "whom", "why", "will", "with", "within", "without", "won", "would", "yes", "yet", "you", "your", "yours",
        "ve", "re", "s", "t", "d", "m", "e", "g", "ie", "eg", "vs", "new", "two", "three", "first", "second",
        "last", "next", "still", "already", "always", "never", "must", "need", "needs", "want", "wants", "way",
        "thing", "things", "something", "anything", "everything", "nothing", "see", "say", "says", "said",
        "okay", "ok", "sure", "well", "really", "lot", "lots", "done", "able", "instead", "rather", "less",
        "part", "parts", "case", "cases", "set", "sets", "put", "run", "runs", "go", "goes", "going", "come",
        "take", "takes", "give", "gives", "keep", "keeps", "find", "found", "know", "look", "looks", "think",
        "try", "tries", "call", "calls", "called", "work", "works", "working", "good", "better", "best", "long",
        "short", "small", "large", "big", "high", "low", "full", "empty", "true", "false", "null", "none",
        "note", "notes", "example", "examples", "e.g", "i.e", "symbols", "time", "times", "file", "files",
        "line", "lines", "function", "functions", "method", "methods", "test", "tests", "unit",
        // Polish
        "aby", "albo", "ale", "ani", "bardzo", "bez", "bo", "by", "być", "był", "była", "było", "były", "będzie",
        "będą", "chce", "chcę", "co", "czy", "czyli", "dla", "do", "dzięki", "gdy", "gdzie", "go", "i", "ich",
        "im", "inne", "inny", "iż", "ja", "jak", "jako", "je", "jego", "jej", "jest", "jeśli", "jeszcze", "już",
        "każdy", "kiedy", "kto", "która", "które", "którego", "której", "który", "których", "którym", "lub",
        "ma", "mają", "mam", "mi", "mnie", "może", "można", "mu", "na", "nad", "nam", "nas", "nawet", "nic",
        "nich", "nie", "niech", "niż", "no", "np", "o", "od", "oraz", "po", "pod", "potem", "przed", "przez",
        "przy", "się", "są", "ta", "tak", "także", "tam", "te", "tego", "tej", "ten", "też", "to", "tu", "tutaj",
        "tylko", "tym", "tzn", "u", "w", "we", "wiec", "więc", "wszystko", "wszystkie", "z", "za", "ze", "że",
        "żeby", "itd", "itp", "sobie", "siebie", "jakie", "jaki", "jaka", "tych", "tymi", "temu", "mieć", "musi",
        "muszą", "powinien", "powinno", "powinna", "trzeba", "należy", "zawsze", "nigdy", "wolno", "można",
        "jednak", "bardziej", "raz", "dwa", "gdyż", "oraz", "obecnie", "teraz", "dopiero", "zamiast", "czym",
        "tym", "cały", "cała", "całe", "sam", "sama", "samo", "nasz", "nasza", "nasze", "wasz", "jeden", "jedna",
        "jedno", "ich", "moje", "mój", "moja", "które", "wtedy", "gdyby", "jeżeli", "każdej", "każda", "każde",
        "każdego", "każdym", "swoje", "swój", "swoją", "tego", "tym", "ale", "dopóki", "dopiero", "jedynie",
        // code keywords / boilerplate
        "std", "const", "return", "returns", "include", "auto", "int", "void", "bool", "string", "str",
        "nullptr", "self", "def", "class", "struct", "public", "private", "protected", "namespace", "template",
        "typename", "static", "inline", "virtual", "override", "elif", "while", "import", "lambda", "except",
        "catch", "throw", "this", "len", "dict", "print", "size", "size_t", "int64", "uint8", "char", "double",
        "float", "unsigned", "explicit", "noexcept", "constexpr", "pragma", "once", "endif", "ifdef", "ifndef",
        "define", "typedef", "enum", "operator", "delete", "default", "case", "break", "continue", "switch",
        "optional", "vector", "unique_ptr", "shared_ptr", "string_view", "json", "value", "values", "args",
        "kwargs", "cls", "http", "https", "www", "com", "org", "txt", "cpp", "hpp", "py", "md", "todo", "fixme",
        "loom_api", "status", "result", "error", "errors", "data", "name", "names", "type", "types", "text",
        "list", "item", "key", "keys", "id", "ids", "fn", "ptr", "ref", "refs", "obj", "tmp", "buf", "len",
        "arg", "param", "params", "var", "val", "src", "dst", "idx", "num", "cur", "out", "min", "max", "ret",
        "res", "req", "resp", "msg", "cfg", "ctx", "impl", "info", "debug", "warn", "warning", "log", "logger",
    };
    std::unordered_set<std::string> s;
    for (const char* w : words) s.insert(w);
    return s;
  }();
  return kSet;
}

bool is_upper_cp(char32_t c) { return unicode::simple_lower(c) != c; }

}  // namespace

std::vector<std::string> tokenize(std::string_view text) {
  std::vector<std::string> out;
  std::u32string cur;
  auto flush = [&] {
    if (!cur.empty() && !all_digits(cur)) out.push_back(utf8::encode(cur));
    cur.clear();
  };
  for (char32_t c : utf8::decode(text)) {
    if (unicode::is_alnum(c)) {
      cur.push_back(unicode::simple_lower(c));
    } else {
      flush();
    }
  }
  flush();
  return out;
}

bool is_stopword(std::string_view lower_token) { return stopwords().count(std::string(lower_token)) > 0; }

std::vector<std::string> content_tokens(std::string_view text) {
  std::vector<std::string> out;
  for (auto& t : tokenize(text)) {
    if (utf8::length(t) < 3 || is_stopword(t)) continue;
    out.push_back(std::move(t));
  }
  return out;
}

std::vector<std::string> candidate_terms(std::string_view text) {
  // Unigrams plus bigrams of adjacent content tokens separated only by
  // spaces or a hyphen ("knowledge graph", "append-only"); paths, "::",
  // punctuation and line breaks end a phrase.
  std::vector<std::string> out;
  std::string prev;
  bool joinable = false;  // only spaces/hyphen since the previous token
  std::u32string cur;
  auto flush = [&] {
    if (cur.empty()) return;
    std::string t = utf8::encode(cur);
    cur.clear();
    bool content = !all_digits(utf8::decode(t)) && utf8::length(t) >= 3 && !is_stopword(t);
    if (!content) {
      prev.clear();
      joinable = false;
      return;
    }
    if (!prev.empty() && joinable && prev != t) out.push_back(prev + " " + t);
    out.push_back(t);
    prev = t;
    joinable = true;
  };
  for (char32_t c : utf8::decode(text)) {
    if (unicode::is_alnum(c)) {
      cur.push_back(unicode::simple_lower(c));
      continue;
    }
    flush();
    if (!(c == ' ' || c == '-')) joinable = false;
  }
  flush();
  return out;
}

namespace {

std::string strip_line_marker(std::string_view line, bool* is_bullet, bool* is_heading) {
  std::string_view s = utf8::strip(line);
  *is_bullet = false;
  *is_heading = false;
  // headings (section context, not sentences)
  std::size_t h = 0;
  while (h < s.size() && s[h] == '#') ++h;
  if (h > 0 && h < s.size() && s[h] == ' ') {
    *is_heading = true;
    return std::string(utf8::strip(s.substr(h)));
  }
  while (!s.empty() && s.front() == '>') {
    s.remove_prefix(1);
    s = utf8::lstrip(s);
  }
  if (s.size() >= 2 && (s[0] == '-' || s[0] == '*' || s[0] == '+') && s[1] == ' ') {
    *is_bullet = true;
    s.remove_prefix(2);
  } else {
    std::size_t d = 0;
    while (d < s.size() && d < 4 && s[d] >= '0' && s[d] <= '9') ++d;
    if (d > 0 && d + 1 < s.size() && (s[d] == '.' || s[d] == ')') && s[d + 1] == ' ') {
      *is_bullet = true;
      s.remove_prefix(d + 2);
    }
  }
  if (!s.empty() && s.front() == '|') *is_bullet = true;  // table row: its own unit
  return std::string(utf8::strip(s));
}

bool is_abbrev_before(const std::u32string& s, std::size_t dot) {
  // word immediately before the dot
  std::size_t b = dot;
  while (b > 0 && unicode::is_alpha(s[b - 1])) --b;
  std::u32string w = s.substr(b, dot - b);
  for (auto& c : w) c = unicode::simple_lower(c);
  static const std::vector<std::u32string> kAbbr = {U"e", U"g", U"i", U"np", U"itd", U"itp", U"tzn", U"etc", U"vs",
                                                   U"cf", U"dr", U"mr", U"ms", U"prof", U"tj", U"in", U"al", U"no",
                                                   U"nr", U"ok", U"ang", U"pl"};
  if (w.size() <= 1 && b > 0 && s[b - 1] == '.') return true;  // "e.g." / "m.in."
  return std::find(kAbbr.begin(), kAbbr.end(), w) != kAbbr.end();
}

void split_paragraph(std::string_view para, std::vector<std::string>& out) {
  std::u32string s = utf8::decode(para);
  std::size_t start = 0;
  auto emit = [&](std::size_t end) {
    std::string piece(utf8::strip(utf8::encode(std::u32string_view(s).substr(start, end - start))));
    if (utf8::length(piece) >= 8 && tokenize(piece).size() >= 2) out.push_back(std::move(piece));
  };
  for (std::size_t i = 0; i < s.size(); ++i) {
    char32_t c = s[i];
    if (c != '.' && c != '!' && c != '?') continue;
    // consume closing punctuation
    std::size_t j = i + 1;
    while (j < s.size() && (s[j] == '.' || s[j] == '!' || s[j] == '?' || s[j] == ')' || s[j] == '"' ||
                            s[j] == U'”' || s[j] == '\'')) {
      ++j;
    }
    if (j >= s.size()) break;
    if (!unicode::is_space(s[j])) continue;
    std::size_t k = j;
    while (k < s.size() && unicode::is_space(s[k])) ++k;
    if (k >= s.size()) break;
    char32_t n = s[k];
    bool starts = is_upper_cp(n) || unicode::is_decimal(n) || n == '"' || n == U'„' || n == U'“' || n == '(' ||
                  n == '[' || n == '*' || n == '`';
    if (!starts) continue;
    if (c == '.' && is_abbrev_before(s, i)) continue;
    emit(j);
    start = k;
    i = k - 1;
  }
  emit(s.size());
}

}  // namespace

std::vector<std::string> split_sentences(std::string_view text) {
  std::vector<std::string> out;
  std::string para;
  bool in_fence = false;
  auto flush = [&] {
    if (!para.empty()) split_paragraph(para, out);
    para.clear();
  };
  std::size_t pos = 0;
  while (pos <= text.size()) {
    std::size_t nl = text.find('\n', pos);
    if (nl == std::string_view::npos) nl = text.size();
    std::string_view line = text.substr(pos, nl - pos);
    pos = nl + 1;
    std::string_view st = utf8::strip(line);
    if (st.substr(0, 3) == "```" || st.substr(0, 3) == "~~~") {
      flush();
      in_fence = !in_fence;
      if (pos > text.size()) break;
      continue;
    }
    if (in_fence) {
      if (pos > text.size()) break;
      continue;
    }
    if (st.empty() || st == "---" || st == "***" || st.find_first_not_of("-|: ") == std::string_view::npos) {
      flush();
    } else {
      bool bullet = false;
      bool heading = false;
      std::string body = strip_line_marker(line, &bullet, &heading);
      if (heading) {
        flush();
      } else if (!body.empty() && body.front() == '|') {
        flush();
        // table row: skip header rows (followed by a |---| separator), join cells
        std::size_t nl2 = text.find('\n', pos);
        std::string_view next = pos <= text.size() ? utf8::strip(text.substr(pos, nl2 == std::string_view::npos ? std::string_view::npos : nl2 - pos)) : std::string_view();
        bool header = !next.empty() && next.front() == '|' && next.find_first_not_of("-|: ") == std::string_view::npos;
        if (!header) {
          std::string row;
          std::size_t b = 1;
          while (b < body.size()) {
            std::size_t e = body.find('|', b);
            if (e == std::string::npos) e = body.size();
            std::string cell(utf8::strip(std::string_view(body).substr(b, e - b)));
            if (!cell.empty()) row += (row.empty() ? "" : " — ") + cell;
            b = e + 1;
          }
          para = row;
          flush();
        }
      } else if (bullet) {
        flush();
        para = body;
      } else {
        if (!para.empty()) para += ' ';
        para += body;
      }
    }
    if (pos > text.size()) break;
  }
  flush();
  return out;
}

std::string first_date(std::string_view t) {
  auto dig = [&](std::size_t i) { return i < t.size() && t[i] >= '0' && t[i] <= '9'; };
  for (std::size_t i = 0; i + 10 <= t.size(); ++i) {
    if (!(dig(i) && dig(i + 1) && dig(i + 2) && dig(i + 3) && t[i + 4] == '-' && dig(i + 5) && dig(i + 6) &&
          t[i + 7] == '-' && dig(i + 8) && dig(i + 9))) {
      continue;
    }
    if (i > 0 && (dig(i - 1) || t[i - 1] == '_' || t[i - 1] == '-')) continue;  // part of a file name / id
    if (dig(i + 10)) continue;
    if (i + 10 < t.size() && t[i + 10] == '.' && i + 11 < t.size() && std::isalpha(static_cast<unsigned char>(t[i + 11]))) {
      continue;  // "..._2026-09-16.md"
    }
    int y = std::stoi(std::string(t.substr(i, 4)));
    int m = std::stoi(std::string(t.substr(i + 5, 2)));
    int d = std::stoi(std::string(t.substr(i + 8, 2)));
    if (y < 1990 || y > 2100 || m < 1 || m > 12 || d < 1 || d > 31) continue;
    return std::string(t.substr(i, 10));
  }
  return "";
}

std::string iso_from_epoch(double seconds) {
  if (!std::isfinite(seconds) || seconds <= 0) return "";
  std::time_t tt = static_cast<std::time_t>(std::floor(seconds));
  std::tm tm{};
  if (!gmtime_r(&tt, &tm)) return "";
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

std::string normalize_date(std::string_view s) {
  s = utf8::strip(s);
  if (s.size() < 10) return "";
  std::string d = first_date(s.substr(0, 10));
  if (d.empty()) return "";
  if (s.size() < 19 || (s[10] != 'T' && s[10] != ' ')) return d;
  auto num = [&](std::size_t i, std::size_t n) -> int {
    int v = 0;
    for (std::size_t k = i; k < i + n; ++k) {
      if (k >= s.size() || s[k] < '0' || s[k] > '9') return -1;
      v = v * 10 + (s[k] - '0');
    }
    return v;
  };
  std::tm tm{};
  tm.tm_year = num(0, 4) - 1900;
  tm.tm_mon = num(5, 2) - 1;
  tm.tm_mday = num(8, 2);
  tm.tm_hour = num(11, 2);
  tm.tm_min = num(14, 2);
  tm.tm_sec = num(17, 2);
  if (tm.tm_hour < 0 || tm.tm_min < 0 || tm.tm_sec < 0) return d;
  std::size_t i = 19;
  if (i < s.size() && s[i] == '.') {
    ++i;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
  }
  long offset = 0;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
    int sign = s[i] == '-' ? -1 : 1;
    int hh = num(i + 1, 2);
    int mm = (i + 3 < s.size() && s[i + 3] == ':') ? num(i + 4, 2) : num(i + 3, 2);
    if (hh >= 0 && mm >= 0) offset = sign * (hh * 3600L + mm * 60L);
  }
  std::time_t t = timegm(&tm) - offset;
  return iso_from_epoch(static_cast<double>(t));
}

std::string date_only(std::string_view iso) { return iso.size() >= 10 ? std::string(iso.substr(0, 10)) : ""; }

std::string hash_prefix(std::string_view s, std::size_t n) { return Sha256::hex(s).substr(0, n); }

std::string clip(std::string_view s, std::size_t max_cp) {
  std::string out;
  bool space = false;
  for (char c : s) {
    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
      space = !out.empty();
      continue;
    }
    if (space) out.push_back(' ');
    space = false;
    out.push_back(c);
  }
  if (utf8::length(out) <= max_cp) return out;
  std::string cut(utf8::prefix(out, max_cp > 1 ? max_cp - 1 : 0));
  while (!cut.empty() && cut.back() == ' ') cut.pop_back();
  return cut + "…";
}

std::vector<std::string> split_identifier(std::string_view ident) {
  std::vector<std::string> out;
  std::u32string s = utf8::decode(ident);
  std::u32string cur;
  auto flush = [&] {
    if (!cur.empty()) {
      for (auto& c : cur) c = unicode::simple_lower(c);
      out.push_back(utf8::encode(cur));
    }
    cur.clear();
  };
  for (std::size_t i = 0; i < s.size(); ++i) {
    char32_t c = s[i];
    if (!unicode::is_alnum(c)) {
      flush();
      continue;
    }
    if (!cur.empty() && is_upper_cp(c)) {
      bool prev_lower = !is_upper_cp(cur.back()) && unicode::is_alpha(cur.back());
      bool next_lower = i + 1 < s.size() && unicode::is_alpha(s[i + 1]) && !is_upper_cp(s[i + 1]);
      bool prev_upper = is_upper_cp(cur.back());
      if (prev_lower || (prev_upper && next_lower) || unicode::is_decimal(cur.back())) flush();
    }
    cur.push_back(c);
  }
  flush();
  return out;
}

std::string stem(std::string_view t) {
  std::string s(t);
  auto ends = [&](std::string_view suf) {
    return s.size() > suf.size() + 2 && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
  };
  if (ends("ies")) return s.substr(0, s.size() - 3) + "y";
  if (ends("sses") || ends("xes") || ends("ches") || ends("shes")) return s.substr(0, s.size() - 2);
  if (ends("s") && !ends("ss") && !ends("us") && !ends("is")) return s.substr(0, s.size() - 1);
  return s;
}

std::string gloss(std::string_view t) {
  // Domain PL -> EN glossary (policy data) so Polish spec text can be matched
  // against English code identifiers in the gap report.
  static const std::unordered_map<std::string, std::string> kGloss = {
      {"pamięć", "memory"},       {"pamięci", "memory"},        {"graf", "graph"},
      {"grafu", "graph"},         {"grafie", "graph"},          {"zadanie", "task"},
      {"zadania", "task"},        {"zadań", "task"},            {"źródło", "source"},
      {"źródła", "source"},       {"źródeł", "source"},         {"szyfrowanie", "encryption"},
      {"szyfrowania", "encryption"}, {"wyszukiwanie", "search"}, {"wyszukiwania", "search"},
      {"rozmowa", "conversation"}, {"rozmowy", "conversation"},  {"rozmów", "conversation"},
      {"załączniki", "attachment"}, {"załączników", "attachment"}, {"głos", "voice"},
      {"synchronizacja", "sync"}, {"synchronizacji", "sync"},   {"wykonanie", "execution"},
      {"wykonania", "execution"}, {"środowisko", "environment"}, {"środowiska", "environment"},
      {"wtyczka", "plugin"},      {"wtyczki", "plugin"},        {"logi", "log"},
      {"logów", "log"},           {"kontekst", "context"},      {"kontekstu", "context"},
      {"dostawca", "provider"},   {"providerzy", "provider"},   {"providerów", "provider"},
      {"wersja", "version"},      {"wersje", "version"},        {"gałąź", "branch"},
      {"historia", "history"},    {"historii", "history"},      {"tytuł", "title"},
      {"tytuły", "title"},        {"surowy", "raw"},            {"szablon", "template"},
      {"szablony", "template"},   {"polityka", "policy"},       {"zdarzenie", "event"},
      {"zdarzenia", "event"},     {"decyzja", "decision"},      {"decyzje", "decision"},
      {"wymagania", "requirement"}, {"panele", "panel"},        {"model", "model"},
      {"modele", "model"},        {"import", "import"},         {"eksport", "export"},
      {"eksporty", "export"},     {"sprzeczności", "contradiction"}, {"braki", "gap"},
      {"chronologia", "timeline"}, {"pliki", "file"},           {"plików", "file"},
      {"kod", "code"},            {"kodu", "code"},             {"testy", "test"},
      {"testów", "test"},         {"magazyn", "store"},         {"klucz", "key"},
      {"klucze", "key"},          {"tożsamość", "identity"},    {"artefakt", "artifact"},
      {"artefakty", "artifact"},  {"artefaktów", "artifact"},   {"widok", "view"},
      {"widoki", "view"},         {"agent", "agent"},           {"agenta", "agent"},
      {"przeglądarka", "browser"}, {"terminal", "terminal"},    {"punkt", "checkpoint"},
      {"surowych", "raw"},         {"surowe", "raw"},             {"surowego", "raw"},
      {"eksportów", "export"},     {"eksportu", "export"},        {"oryginału", "source"},
      {"oryginał", "source"},      {"chronologię", "timeline"},   {"forków", "fork"},
      {"forki", "fork"},           {"słownika", "vocabulary"},    {"słownik", "vocabulary"},
      {"wyszukiwania", "search"},  {"terminów", "term"},          {"sprzeczności", "contradiction"},
      {"tematów", "theme"},        {"idei", "idea"},              {"decyzji", "decision"},
      {"scalenie", "merge"},       {"materializacja", "materialize"}, {"grafie", "graph"},
      {"wznowienie", "resume"},   {"ponowienie", "retry"},      {"wycofanie", "rollback"},
  };
  auto it = kGloss.find(std::string(t));
  return it == kGloss.end() ? std::string(t) : it->second;
}

}  // namespace loom::archive
