#!/usr/bin/env python3
"""Text-language files (lang/README.md): split the game's texts for translating, check translations, merge parts.

  lang_po.py split SOURCE.po N OUTDIR      every entry of SOURCE (msgctxt + English msgid, as in lang/game/pl.po) with
                                           an empty msgstr, in N parts of about equal size, at the file sections
  lang_po.py check FILE.po [--encoding E]  the rules translators keep (exit 1 on any problem): every msgstr filled;
                                           [TOKENS], ~tokens, %s/%d, {keys}, the subtitle timing prefix ("12.34|15.60|"
                                           or "5|") and leading/trailing spaces as in msgid; no fewer "|" breaks; only
                                           characters the encoding has (default cp1252: the game's Latin fonts)
  lang_po.py merge HEADER.txt OUT.po PART.po...   the header, then the parts' entries in order
"""
import re
import sys


def unquote(lines):
    out = []
    for line in lines:
        m = re.match(r'^\s*"(.*)"\s*$', line)
        if m:
            out.append(m.group(1).encode('utf-8').decode('unicode_escape').encode('latin-1').decode('utf-8'))
    return ''.join(out)


def quote(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n').replace('\t', '\\t') + '"'


def parse(path):
    """Entries as dicts: comments (lines before), ctxt, id, str (None for the header entry's fields kept raw)."""
    entries, comments, cur, field = [], [], None, None
    for raw in open(path, encoding='utf-8').read().split('\n'):
        line = raw.rstrip('\r')
        if line.startswith('msgctxt '):
            cur = {'comments': comments, 'ctxt': [line[8:]], 'id': [], 'str': []}
            comments, field = [], 'ctxt'
        elif line.startswith('msgid '):
            if cur is None or cur['id']:
                cur = {'comments': comments, 'ctxt': None, 'id': [], 'str': []}
                comments = []
            cur['id'] = [line[6:]]
            field = 'id'
        elif line.startswith('msgstr '):
            cur['str'] = [line[7:]]
            field = 'str'
            entries.append(cur)
        elif line.startswith('"') and field and cur is not None:
            cur[field].append(line)
        elif line.startswith('#'):
            comments.append(line)
            field = None
        elif not line.strip():
            field = None if field == 'str' else field
    for e in entries:
        e['ctxt_text'] = unquote(e['ctxt']) if e['ctxt'] else None
        e['id_text'] = unquote(e['id'])
        e['str_text'] = unquote(e['str'])
    return entries


def write_entry(f, e, msgstr):
    for c in e['comments']:
        f.write(c + '\n')
    if e['ctxt_text'] is not None:
        f.write('msgctxt ' + quote(e['ctxt_text']) + '\n')
    f.write('msgid ' + quote(e['id_text']) + '\n')
    f.write('msgstr ' + quote(msgstr) + '\n\n')


def split(source, n, outdir):
    entries = [e for e in parse(source) if e['id_text']]
    size = -(-len(entries) // n)
    for i in range(n):
        part = entries[i * size:(i + 1) * size]
        with open(f'{outdir}/part{i + 1}.po', 'w', encoding='utf-8') as f:
            for e in part:
                write_entry(f, e, '')
        print(f'part{i + 1}.po: {len(part)} entries')


TIMING = re.compile(r'^(\d+(?:\.\d+)?\|(?:\d+(?:\.\d+)?\|)?)')


def problems(e, encoding):
    src, dst = e['id_text'], e['str_text']
    if not dst:
        return ['empty msgstr']
    out = []
    for name, pattern in (('[TOKEN]', r'\[[A-Z0-9_ ]+\]'), ('~token', r'~[A-Za-z0-9_]+'), ('%', r'%[sdi]'),
                          ('{key}', r'\{[a-z-]+\}')):
        if sorted(re.findall(pattern, src)) != sorted(re.findall(pattern, dst)):
            out.append(f'{name}s differ: {re.findall(pattern, src)} vs {re.findall(pattern, dst)}')
    ts, td = TIMING.match(src), TIMING.match(dst)
    if (ts.group(1) if ts else None) != (td.group(1) if td else None):
        out.append('subtitle timing prefix differs')
    body_src = src[ts.end():] if ts else src
    body_dst = dst[td.end():] if td else dst
    if body_dst.count('|') < body_src.count('|'):
        out.append('fewer | breaks')
    edge = ' \u00a0'  # a no-break space counts as a space
    if len(src) - len(src.lstrip(edge)) != len(dst) - len(dst.lstrip(edge)) or \
            len(src) - len(src.rstrip(edge)) != len(dst) - len(dst.rstrip(edge)):
        out.append('leading/trailing spaces differ')
    try:
        dst.encode(encoding)
    except UnicodeEncodeError as err:
        out.append(f'character not in {encoding}: {dst[err.start:err.end]!r}')
    return out


def check(path, encoding):
    bad = 0
    entries = [e for e in parse(path) if e['id_text']]
    for e in entries:
        for p in problems(e, encoding):
            bad += 1
            print(f'{path}: {e["ctxt_text"] or ""} {e["id_text"][:60]!r}: {p}')
    print(f'{path}: {len(entries)} entries, {bad} problems')
    return bad == 0


def merge(header, out, parts):
    with open(out, 'w', encoding='utf-8') as f:
        f.write(open(header, encoding='utf-8').read().rstrip('\n') + '\n\n')
        n = 0
        for part in parts:
            for e in parse(part):
                if e['id_text']:
                    write_entry(f, e, e['str_text'])
                    n += 1
    print(f'{out}: {n} entries')


if __name__ == '__main__':
    a = sys.argv[1:]
    if a and a[0] == 'split' and len(a) == 4:
        split(a[1], int(a[2]), a[3])
    elif a and a[0] == 'check' and len(a) >= 2:
        enc = a[a.index('--encoding') + 1] if '--encoding' in a else 'cp1252'
        sys.exit(0 if check(a[1], enc) else 1)
    elif a and a[0] == 'merge' and len(a) >= 4:
        merge(a[1], a[2], a[3:])
    else:
        print(__doc__)
        sys.exit(2)
