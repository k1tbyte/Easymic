using System; using System.Collections.Generic; using System.IO; using System.Linq; using System.Text;

// Punto ps.dat rule emulator: B=word begin, P=whole word, E=exception, C=case-sensitive, none/A=anywhere
public static class PuntoSim {
  static Dictionary<string, HashSet<string>> _sets = new Dictionary<string, HashSet<string>>();
  static int _max = 0;
  static Dictionary<char, char> _ruToEn = new Dictionary<char, char>(), _enToRu = new Dictionary<char, char>();

  static HashSet<string> Set(string k) { HashSet<string> h; if (!_sets.TryGetValue(k, out h)) { h = new HashSet<string>(); _sets[k] = h; } return h; }
  static HashSet<string> Get(string k) { HashSet<string> h; _sets.TryGetValue(k, out h); return h; }
  public static bool IsCyr(string s) { return s.Any(c => c >= '\u0400' && c <= '\u04FF'); }

  public static string Load(string path) {
    const string en = "`1234567890-=qwertyuiop[]\\asdfghjkl;'zxcvbnm,./~!@#$%^&*()_+QWERTYUIOP{}|ASDFGHJKL:\"ZXCVBNM<>?";
    const string ru = "ё1234567890-=йцукенгшщзхъ\\фывапролджэячсмитьбю.Ё!\"№;%:?*()_+ЙЦУКЕНГШЩЗХЪ/ФЫВАПРОЛДЖЭЯЧСМИТЬБЮ,";
    for (int i = 0; i < en.Length; i++) { _enToRu[en[i]] = ru[i]; _ruToEn[ru[i]] = en[i]; }
    var counts = new SortedDictionary<string, int>();
    foreach (var raw in File.ReadAllLines(path, Encoding.UTF8)) {
      if (raw.Length == 0 || raw.StartsWith("PSVersion")) continue;
      string tag = "", pat = raw;
      if (raw.StartsWith("_")) { int sp = raw.IndexOf(' '); tag = raw.Substring(1, sp - 1); pat = raw.Substring(sp + 1); }
      if (tag.Contains("D")) continue;
      string lang = IsCyr(pat) ? "ru" : "en";
      bool ex = tag.Contains("E"), whole = tag.Contains("P"), beg = !whole && tag.Contains("B"), cs = tag.Contains("C");
      string kind = (ex ? "X" : "") + (whole ? "Whole" : beg ? "Beg" : "Any");
      Set((cs ? "c" : "") + lang + kind).Add(cs ? pat : pat.ToLowerInvariant());
      _max = Math.Max(_max, pat.Length);
      int c; counts.TryGetValue(lang + kind, out c); counts[lang + kind] = c + 1;
    }
    return string.Join(", ", counts.Select(kv => kv.Key + "=" + kv.Value));
  }

  public static string Convert(string w) {
    bool cyr = IsCyr(w); var sb = new StringBuilder();
    foreach (char ch in w) { char o; sb.Append((cyr ? _ruToEn : _enToRu).TryGetValue(ch, out o) ? o : ch); }
    return sb.ToString();
  }

  // Earliest prefix length at which a trigger completes; 0 = none
  public static int Fire(string w, string lang, out string pat) {
    pat = null; string lw = w.ToLowerInvariant();
    for (int e = 1; e <= w.Length; e++) {
      for (int c = 0; c < 2; c++) {
        string s = c == 1 ? w : lw, p = c == 1 ? "c" : "";
        var beg = Get(p + lang + "Beg"); var any = Get(p + lang + "Any");
        if (beg != null && e <= _max && beg.Contains(s.Substring(0, e))) { pat = "B " + s.Substring(0, e); return e; }
        if (any != null) {
          string padded = " " + s; int pe = e + 1;
          for (int k = 1; k <= _max && k <= pe; k++) { string sub = padded.Substring(pe - k, k); if (any.Contains(sub)) { pat = "A " + sub; return e; } }
        }
      }
    }
    for (int c = 0; c < 2; c++) {
      string s = c == 1 ? w : lw, p = c == 1 ? "c" : "";
      var whole = Get(p + lang + "Whole"); var any = Get(p + lang + "Any");
      if (whole != null && whole.Contains(s)) { pat = "P " + s; return w.Length; }
      if (any != null) { string padded = " " + s + " "; for (int k = 1; k <= _max && k <= padded.Length; k++) { string sub = padded.Substring(padded.Length - k); if (any.Contains(sub)) { pat = "A$ " + sub; return w.Length; } } }
    }
    return 0;
  }

  public static bool Excepted(string w, string lang, out string pat) {
    pat = null; string lw = w.ToLowerInvariant();
    for (int c = 0; c < 2; c++) {
      string s = c == 1 ? w : lw, p = c == 1 ? "c" : "";
      var whole = Get(p + lang + "XWhole"); var beg = Get(p + lang + "XBeg"); var any = Get(p + lang + "XAny");
      if (whole != null && whole.Contains(s)) { pat = "XP " + s; return true; }
      if (beg != null) for (int k = 1; k <= Math.Min(s.Length, _max); k++) if (beg.Contains(s.Substring(0, k))) { pat = "XB " + s.Substring(0, k); return true; }
      if (any != null) { string padded = " " + s + " "; for (int i = 0; i < padded.Length; i++) for (int k = 1; k <= _max && i + k <= padded.Length; k++) if (any.Contains(padded.Substring(i, k))) { pat = "XA " + padded.Substring(i, k); return true; } }
    }
    return false;
  }

  public static bool Switches(string w, out int pos, out string why) {
    string lang = IsCyr(w) ? "ru" : "en", x;
    pos = Fire(w, lang, out why);
    if (pos == 0) return false;
    if (Excepted(w, lang, out x)) { why += " / " + x; return false; }
    return true;
  }

  public static string Eval(string label, IList<string> words, bool expectSwitch, int show) {
    int hits = 0, mid = 0, midUnsafe = 0; double frac = 0; var examples = new List<string>();
    foreach (var w in words) {
      int pos; string why; bool sw = Switches(w, out pos, out why);
      if (pos > 0 && pos < w.Length && !sw) midUnsafe++;
      if (sw) { hits++; frac += (double)pos / w.Length; if (pos < w.Length) mid++; }
      if (sw != expectSwitch && examples.Count < show) examples.Add(w + " [" + (why ?? "no rule") + "]");
    }
    var sb = new StringBuilder();
    sb.AppendFormat("{0,-26} n={1,-6} switched={2,-6} ({3:P1})", label, words.Count, hits, words.Count == 0 ? 0 : (double)hits / words.Count);
    if (expectSwitch && hits > 0) sb.AppendFormat("  mid-word={0:P0} avg-at={1:P0} of word", (double)mid / hits, frac / hits);
    if (!expectSwitch && midUnsafe > 0) sb.AppendFormat("  mid-word-then-excepted={0}", midUnsafe);
    if (examples.Count > 0) sb.Append("\n    " + (expectSwitch ? "missed: " : "false: ") + string.Join(" | ", examples));
    return sb.ToString();
  }
}
