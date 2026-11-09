#!/usr/bin/env python3
"""Build SFZ instrument maps for VSCO 2 CE from its sample file names.

usage: make_vsco_sfz.py <files.txt> <out_dir> [MappingChart.txt] [gains.json]
files.txt: one path per line, relative to the VSCO 2 CE folder.
Writes the .sfz files into out_dir; they belong in the VSCO 2 CE folder root.
"""
import re
import sys
from collections import defaultdict
from pathlib import PurePosixPath

NOTE_RE = re.compile(r'^([A-Ga-g])([#b]?)(-?\d)$')
SEMI = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}
DYN = ['ppp', 'pp', 'p', 'mp', 'mf', 'f', 'ff', 'fff']
DYN_WORDS = {'soft': 'p', 'quiet': 'p', 'medium': 'mf', 'loud': 'f'}
MIC_PREF = ['sum', 'main', 'mid', 'close', 'far']


def note_to_midi(tok, c_octave_of_60):
    m = NOTE_RE.match(tok)
    if not m:
        return None
    n = SEMI[m.group(1).lower()] + (1 if m.group(2) == '#' else -1 if m.group(2) == 'b' else 0)
    return n + 12 * (int(m.group(3)) - c_octave_of_60) + 60


def parse(stem, c_octave_of_60):
    """-> (midi or None, layer rank, rr index, mic, sound id without dyn/rr/mic)"""
    toks = re.split(r'[_]', stem)
    midi = None
    layer = None
    rr = 1
    mic = ''
    rest = []
    for i, t in enumerate(toks):
        tl = t.lower()
        if midi is None and note_to_midi(t, c_octave_of_60) is not None and i > 0:
            midi = note_to_midi(t, c_octave_of_60)
            continue
        m = re.match(r'^v(\d+)$', tl) or re.match(r'^dyn(\d+)$', tl)
        if m:
            layer = int(m.group(1))
            continue
        if tl in DYN:
            layer = 10 + DYN.index(tl)
            continue
        if tl in DYN_WORDS:
            layer = 10 + DYN.index(DYN_WORDS[tl])
            continue
        m = re.match(r'^rr(\d+)$', tl)
        if m:
            rr = int(m.group(1))
            continue
        if tl in MIC_PREF:
            mic = tl
            continue
        m = re.match(r'^(sustain|staccato)(\d+)$', tl)
        if m:
            rr = int(m.group(2))
            continue
        if re.match(r'^\d+$', tl) and i > 0:
            rr = int(tl)
            continue
        rest.append(t)
    return midi, (layer if layer is not None else 0), rr, mic, '_'.join(rest)


# ---------------------------------------------------------------- instruments
# (folder, display name, octave of middle C in the names, style)
#   style: 'sus' = held notes, 'short' = one-shot, 'piano' = stops on release slowly
ORCH = 3   # orchestral sets name middle C "C3"
KEYS = 4   # keyboard / percussion sets name middle C "C4"
PITCHED = [
    ('Strings/Violin Section/susVib', 'Violins · sustain', ORCH, 'sus'),
    ('Strings/Violin Section/Trem', 'Violins · tremolo', ORCH, 'sus'),
    ('Strings/Violin Section/Spic', 'Violins · spiccato', ORCH, 'short'),
    ('Strings/Violin Section/Pizz', 'Violins · pizzicato', ORCH, 'short'),
    ('Strings/Viola Section/susvib', 'Violas · sustain', ORCH, 'sus'),
    ('Strings/Viola Section/trem', 'Violas · tremolo', ORCH, 'sus'),
    ('Strings/Viola Section/spic', 'Violas · spiccato', ORCH, 'short'),
    ('Strings/Viola Section/pizz', 'Violas · pizzicato', ORCH, 'short'),
    ('Strings/Cello Section/susvib', 'Cellos · sustain', ORCH, 'sus'),
    ('Strings/Cello Section/trem', 'Cellos · tremolo', ORCH, 'sus'),
    ('Strings/Cello Section/spic', 'Cellos · spiccato', ORCH, 'short'),
    ('Strings/Cello Section/pizzT', 'Cellos · pizzicato', ORCH, 'short'),
    ('Strings/Solo Violin/Arco Vib', 'Solo violin · arco', ORCH, 'sus'),
    ('Strings/Solo Violin/Trem', 'Solo violin · tremolo', ORCH, 'sus'),
    ('Strings/Solo Violin/spic', 'Solo violin · spiccato', ORCH, 'short'),
    ('Strings/Solo Violin/Pizz', 'Solo violin · pizzicato', ORCH, 'short'),
    ('Strings/Solo Contrabass/SusVib', 'Contrabass · sustain vibrato', ORCH, 'sus'),
    ('Strings/Solo Contrabass/SusNV', 'Contrabass · sustain', ORCH, 'sus'),
    ('Strings/Solo Contrabass/Trem', 'Contrabass · tremolo', ORCH, 'sus'),
    ('Strings/Solo Contrabass/Spic', 'Contrabass · spiccato', ORCH, 'short'),
    ('Strings/Solo Contrabass/Pizz', 'Contrabass · pizzicato', ORCH, 'short'),
    ('Strings/Harp', 'Harp', KEYS, 'short'),
    ('Woodwinds/Flute/susvib', 'Flute · sustain vibrato', ORCH, 'sus'),
    ('Woodwinds/Flute/susNV', 'Flute · sustain', ORCH, 'sus'),
    ('Woodwinds/Flute/expvib', 'Flute · expressive', ORCH, 'sus'),
    ('Woodwinds/Flute/stac', 'Flute · staccato', ORCH, 'short'),
    ('Woodwinds/Piccolo/Sus', 'Piccolo · sustain', ORCH, 'sus'),
    ('Woodwinds/Piccolo/Stac', 'Piccolo · staccato', ORCH, 'short'),
    ('Woodwinds/Oboe/Vib', 'Oboe · vibrato', ORCH, 'sus'),
    ('Woodwinds/Oboe/Sus', 'Oboe · sustain', ORCH, 'sus'),
    ('Woodwinds/Oboe/Stacc', 'Oboe · staccato', ORCH, 'short'),
    ('Woodwinds/Clarinet/susLong', 'Clarinet · sustain', ORCH, 'sus'),
    ('Woodwinds/Clarinet/stac', 'Clarinet · staccato', ORCH, 'short'),
    ('Woodwinds/Bassoon/vib', 'Bassoon · vibrato', ORCH, 'sus'),
    ('Woodwinds/Bassoon/sus', 'Bassoon · sustain', ORCH, 'sus'),
    ('Woodwinds/Bassoon/stac', 'Bassoon · staccato', ORCH, 'short'),
    ('Brass/F Horn/sus', 'French horn · sustain', ORCH, 'sus'),
    ('Brass/F Horn/mute', 'French horn · muted', ORCH, 'sus'),
    ('Brass/F Horn/stac', 'French horn · staccato', ORCH, 'short'),
    ('Brass/Trumpet/sus', 'Trumpet · sustain', ORCH, 'sus'),
    ('Brass/Trumpet/susvib', 'Trumpet · vibrato', ORCH, 'sus'),
    ('Brass/Trumpet/straightM-sus', 'Trumpet · straight mute', ORCH, 'sus'),
    ('Brass/Trumpet/harmonM-sus', 'Trumpet · harmon mute', ORCH, 'sus'),
    ('Brass/Trumpet/stac', 'Trumpet · staccato', ORCH, 'short'),
    ('Brass/Tenor Trombone/sus', 'Trombone · sustain', ORCH, 'sus'),
    ('Brass/Tenor Trombone/vib', 'Trombone · vibrato', ORCH, 'sus'),
    ('Brass/Tenor Trombone/stac', 'Trombone · staccato', ORCH, 'short'),
    ('Brass/OldTrombone/Sustain', 'Old trombone · sustain', ORCH, 'sus'),
    ('Brass/OldTrombone/Vibrato', 'Old trombone · vibrato', ORCH, 'sus'),
    ('Brass/OldTrombone/Short', 'Old trombone · short', ORCH, 'short'),
    ('Brass/OldTrombone/Buzz', 'Old trombone · buzz', ORCH, 'sus'),
    ('Brass/Tuba/sus', 'Tuba · sustain', ORCH, 'sus'),
    ('Brass/Tuba/stac', 'Tuba · staccato', ORCH, 'short'),
    ('Keys/Upright Nr1', 'Upright piano 1', KEYS, 'piano'),
    ('Percussion/Glock', 'Glockenspiel', KEYS, 'short'),
    ('Percussion/Marimba', 'Marimba', KEYS, 'short'),
    ('Percussion/Xylo', 'Xylophone', KEYS, 'short'),
]

# organ sets are numbered by key on the manual: (folder, file prefix, number of key 1, midi of key 1)
ORGAN = [
    ('Organ · loud', [('Keys/Organ/Loud', 'Rode_Man3Open_', 1, 36), ('Keys/Organ/Loud', 'Rode_Pedal_', 1, 24)]),
    ('Organ · quiet', [('Keys/Organ/Quiet', 'NT5_Man3Quiet_', 122, 36), ('Keys/Organ/Quiet', 'NT5_PedalQuiet_', 64, 24)]),
]

# drum kits: every distinct sound gets its own key from C2 upward
KITS = [
    ('Percussion kit', ['Percussion/']),
    ('Timpani', ['Percussion/Timpani/']),
    ('Drums (VSCO 1)', ['VSCO 1 Percussion/drums/']),
    ('Metal percussion (VSCO 1)', ['VSCO 1 Percussion/varMetal/']),
    ('Wood percussion (VSCO 1)', ['VSCO 1 Percussion/varWood/']),
]


def header(style):
    if style == 'sus':
        return 'ampeg_attack=0.005 ampeg_release=0.35'
    if style == 'piano':
        return 'ampeg_release=0.9'
    return 'loop_mode=one_shot ampeg_release=0.15'


def layer_ranges(layers):
    """sorted distinct layer ranks -> {rank: (lovel, hivel)}"""
    n = len(layers)
    out = {}
    for i, l in enumerate(layers):
        lo = 1 + (127 * i) // n
        hi = (127 * (i + 1)) // n
        out[l] = (lo, hi)
    return out


def pick_mic(entries):
    """entries with the same note/layer/rr but different mic: keep one"""
    best = {}
    for e in entries:
        key = (e['midi'], e['layer'], e['rr'])
        rank = MIC_PREF.index(e['mic']) if e['mic'] in MIC_PREF else -1
        if key not in best or rank < best[key][0]:
            best[key] = (rank, e)
    return [v[1] for v in best.values()]


def pitched_regions(entries, spread=4):
    entries = pick_mic(entries)
    layers = sorted({e['layer'] for e in entries})
    ranges = layer_ranges(layers)
    lines = []
    for l in layers:
        le = [e for e in entries if e['layer'] == l]
        notes = sorted({e['midi'] for e in le})
        for i, n in enumerate(notes):
            lo = notes[i - 1] + 1 + (n - notes[i - 1] - 1) // 2 if i > 0 else max(0, n - spread)
            hi = n + (notes[i + 1] - n - 1) // 2 if i + 1 < len(notes) else min(127, n + spread)
            rrs = sorted([e for e in le if e['midi'] == n], key=lambda e: e['rr'])
            for k, e in enumerate(rrs):
                rnd = ''
                if len(rrs) > 1:
                    rnd = ' lorand=%.3f hirand=%.3f' % (k / len(rrs), (k + 1) / len(rrs))
                lines.append('<region> lokey=%d hikey=%d pitch_keycenter=%d lovel=%d hivel=%d%s sample=%s'
                             % (lo, hi, n, ranges[l][0], ranges[l][1], rnd, e['path']))
    return lines


GAINS = {}


def write(out_dir, name, body_lines, style, note):
    safe = name.replace('·', '-').replace('/', '-').replace('(', '').replace(')', '')
    safe = re.sub(r'\s+', ' ', safe).strip()
    p = PurePosixPath(out_dir) / ('VSCO - ' + safe + '.sfz')
    gain = GAINS.get(PurePosixPath(p).name, 0.0)
    text = ['// VSCO 2 CE · %s' % name,
            '// Map made for Tape by Laurence Who Studio. Samples: Versilian Studios (CC0).',
            '// %s' % note,
            '<group> volume=%.1f %s' % (gain, header(style))]
    text += body_lines
    with open(str(p), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(text) + '\n')
    return p


def main():
    import json, os
    gp = sys.argv[4] if len(sys.argv) > 4 else os.path.join(os.path.dirname(os.path.abspath(sys.argv[1])), 'gains.json')
    if os.path.exists(gp):
        GAINS.update(json.load(open(gp)))
    files = [l.strip() for l in open(sys.argv[1], encoding='utf-8') if l.strip().lower().endswith('.wav')]
    out_dir = sys.argv[2]
    made = []

    for folder, name, coct, style in PITCHED:
        entries = []
        for f in files:
            pp = PurePosixPath(f)
            if str(pp.parent) != folder:
                continue
            midi, layer, rr, mic, _ = parse(pp.stem, coct)
            if midi is None:
                continue
            entries.append(dict(path=f, midi=midi, layer=layer, rr=rr, mic=mic))
        if not entries:
            print('!! nothing parsed in', folder)
            continue
        made.append((write(out_dir, name, pitched_regions(entries), style, 'pitched, %d samples' % len(entries)), len(entries)))

    # upright piano 2: the set ships a chart from file number to key
    chart = {}
    try:
        for line in open(sys.argv[3], encoding='utf-8'):
            m = re.match(r'^(\d+)=(\d+)', line.strip())
            if m:
                chart[int(m.group(1))] = int(m.group(2))
    except (IndexError, OSError):
        pass
    if chart:
        entries = []
        for f in files:
            pp = PurePosixPath(f)
            if str(pp.parent) != 'Keys/Upright Piano':
                continue
            m = re.match(r'^Player_dyn(\d+)_rr(\d+)_(\d+)$', pp.stem)
            if m and int(m.group(3)) in chart:
                entries.append(dict(path=f, midi=chart[int(m.group(3))], layer=int(m.group(1)), rr=int(m.group(2)), mic=''))
        if entries:
            made.append((write(out_dir, 'Upright piano 2', pitched_regions(entries), 'piano', 'pitched, %d samples' % len(entries)), len(entries)))

    for name, parts in ORGAN:
        entries = []
        for folder, prefix, first, midi0 in parts:
            for f in files:
                pp = PurePosixPath(f)
                if str(pp.parent) != folder or not pp.stem.startswith(prefix):
                    continue
                m = re.match(r'^(\d+)', pp.stem[len(prefix):])
                if not m:
                    continue
                entries.append(dict(path=f, midi=midi0 + int(m.group(1)) - first, layer=0, rr=1, mic=''))
        # manual and pedal overlap: keep the manual from C2 up, the pedal below
        manual = [e for e in entries if 'Pedal' not in e['path']]
        pedal = [e for e in entries if 'Pedal' in e['path'] and e['midi'] < 36]
        lines = pitched_regions(pedal, 0) + pitched_regions(manual, 0)
        # stretch the edges a little
        made.append((write(out_dir, name, lines, 'sus', 'pedal below C2, manual from C2'), len(entries)))

    for name, prefixes in KITS:
        sounds = defaultdict(list)
        for f in files:
            pp = PurePosixPath(f)
            if not any(f.startswith(px) for px in prefixes):
                continue
            # 'Percussion kit' = only files directly in Percussion/, not its pitched subfolders
            if name == 'Percussion kit' and str(pp.parent) != 'Percussion':
                continue
            if name == 'Timpani' and 'Timpani' not in f:
                continue
            _, layer, rr, mic, sid = parse(pp.stem, KEYS)
            sid = re.sub(r'\d+$', '', sid) if re.search(r'(?i)(hit|tap|roll)\d+$', sid) is None else sid
            sounds[(str(pp.parent), sid)].append(dict(path=f, layer=layer, rr=rr, mic=mic))
        key = 36
        lines = []
        legend = []
        for (parent, sid), ents in sorted(sounds.items()):
            if key > 108:
                break
            ents = pick_mic([dict(e, midi=key) for e in ents])
            layers = sorted({e['layer'] for e in ents})
            ranges = layer_ranges(layers)
            for l in layers:
                rrs = sorted([e for e in ents if e['layer'] == l], key=lambda e: (e['rr'], e['path']))
                for k, e in enumerate(rrs):
                    rnd = ''
                    if len(rrs) > 1:
                        rnd = ' lorand=%.3f hirand=%.3f' % (k / len(rrs), (k + 1) / len(rrs))
                    lines.append('<region> key=%d lovel=%d hivel=%d%s sample=%s' % (key, ranges[l][0], ranges[l][1], rnd, e['path']))
            legend.append('//   %3d  %s' % (key, (PurePosixPath(parent).name + ' · ' + sid).strip(' ·')))
            key += 1
        if lines:
            made.append((write(out_dir, name, ['// keys:'] + legend + lines, 'short', 'one sound per key from C2'), len(lines)))

    for p, n in made:
        print('%5d  %s' % (n, p))


if __name__ == '__main__':
    main()
