#!/usr/bin/env python3
# coding=utf-8
"""
Extract the (peripheral signal, pad, mux) table out of a board's RTE_Device_917.h.

The Si91x mux number is not derivable from the signal: the same function on a
different pad takes a different mux value (PWM_1H is mux 10 on pad 7 and mux 8
on pad 65). That mapping only exists as data, and the RTE header is where
Silicon Labs ships it -- as compile-time #if alternatives keyed on
RTE_<SIGNAL>_PORT_ID, so a build only ever sees one branch of it.

This walks every branch instead, which is what a runtime pinmux needs.

    ./extract_rte_pinmux.py <RTE_Device_917.h> [--json|--summary]
"""
import argparse
import json
import re
import sys
from collections import defaultdict

# "#define RTE_<SIGNAL>_<FIELD> <value>". The value is taken to end of line
# rather than as a single token: nearly a third of the PIN definitions in this
# header are expressions -- "(7 + GPIO_MAX_PIN)", "(2U)" -- and a pattern that
# only accepts one token skips them silently, which then slides every following
# field of that branch onto the wrong variant.
DEFINE = re.compile(r'^\s*#define\s+RTE_([A-Z0-9_]+?)_(PORT|PIN|MUX|PAD|CHANNEL)\s+(.+?)\s*$')
# Signals whose PORT_ID selector we must not mistake for a field.
PORT_ID = re.compile(r'^\s*#define\s+RTE_([A-Z0-9_]+?)_PORT_ID\s+(.+?)\s*$')
# Trailing "// no pad" style comments are not part of the value.
TRAILING_COMMENT = re.compile(r'\s*(//|/\*).*$')

# ULP pads are addressed in the HP numbering space at 64 + index, which the
# header spells as "(n + GPIO_MAX_PIN)". Resolving it here keeps every row in
# one numbering scheme -- the same one tkl_gpio.c's pin table uses.
CONSTANTS = {"GPIO_MAX_PIN": 64}


def parse(path):
    """Collect every RTE_<signal>_<field> definition, including shadowed ones.

    A signal appears once per #if branch, so the same name legitimately carries
    several pin/mux pairs. They are kept in source order and grouped later; the
    branch condition itself is not evaluated -- the point is the full set.
    """
    entries = defaultdict(list)   # signal -> [ {field: value}, ... ]
    current = defaultdict(dict)

    with open(path, encoding='utf-8', errors='replace') as fh:
        for lineno, line in enumerate(fh, 1):
            if PORT_ID.match(line):
                continue
            m = DEFINE.match(line)
            if not m:
                continue
            signal, field = m.group(1), m.group(2)
            value = TRAILING_COMMENT.sub('', m.group(3)).strip()

            # Branches are delimited by PORT, which every #if arm defines first.
            # Banking on "any repeated field" instead would mis-split whenever a
            # field is missing from one arm, and quietly pair fields that came
            # from different pin options.
            if field == 'PORT' and current[signal]:
                entries[signal].append(current[signal])
                current[signal] = {}
            current[signal][field] = value
            current[signal].setdefault('_line', lineno)

    for signal, pending in current.items():
        if pending:
            entries[signal].append(pending)
    return entries


def numeric(value):
    """Resolve an RTE value to an integer where the header makes that possible.

    Handles the three shapes this header actually uses: a bare literal, a
    parenthesised literal with an optional U suffix, and "(<n> + GPIO_MAX_PIN)".
    Anything else -- typically a pintool macro the SLC config supplies, such as
    "(USART0_TX_PIN + GPIO_MAX_PIN)" -- stays unresolved and is reported rather
    than guessed at.
    """
    if value is None:
        return None
    v = value.strip()
    if v.startswith('(') and v.endswith(')'):
        v = v[1:-1].strip()
    v = re.sub(r'(?<=\d)[uU][lL]?[lL]?\b', '', v)

    try:
        return int(v, 0)
    except ValueError:
        pass

    m = re.fullmatch(r'([A-Za-z_]\w*|\d+)\s*\+\s*([A-Za-z_]\w*|\d+)', v)
    if m:
        parts = []
        for tok in m.groups():
            if tok in CONSTANTS:
                parts.append(CONSTANTS[tok])
            else:
                try:
                    parts.append(int(tok, 0))
                except ValueError:
                    return None
        return parts[0] + parts[1]
    return None


def rows(entries):
    out = []
    for signal, variants in sorted(entries.items()):
        for v in variants:
            pin, mux = numeric(v.get('PIN', '')), numeric(v.get('MUX', ''))
            out.append({
                'signal': signal,
                'port': v.get('PORT'),
                'pin': pin,
                'mux': mux,
                'pad': numeric(v.get('PAD', '')),
                'line': v.get('_line'),
                # Entries whose PIN or MUX is a macro rather than a literal are
                # pintool-driven and cannot be resolved without the generated
                # config; flagged rather than dropped so nothing is lost silently.
                'symbolic': pin is None or mux is None,
                'raw_pin': v.get('PIN'),
                'raw_mux': v.get('MUX'),
            })
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('header')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--summary', action='store_true')
    args = ap.parse_args()

    table = rows(parse(args.header))

    if args.json:
        json.dump(table, sys.stdout, indent=2)
        return

    resolved = [r for r in table if not r['symbolic'] and r['mux'] is not None]
    symbolic = [r for r in table if r['symbolic']]

    if args.summary:
        # Group by peripheral block, not by signal: I2C1_SCL and I2C1_SDA are
        # the same block and it is the block a caller asks for.
        family = defaultdict(set)
        for r in resolved:
            m = re.match(r'^(ULP_UART|USART\d*|UART\d*|I2C\d*|I2S\d*|GSPI|SSI|SIO|PWM|SCT|QEI|OPAMP\d*|COMP\d*|'
                         r'BUTTON\d*|LED\d*)', r['signal'])
            family[m.group(1) if m else r['signal'].split('_')[0]].add((r['signal'], r['pin'], r['mux']))
        print('resolved %d, symbolic (pintool mirror) %d, total %d'
              % (len(resolved), len(symbolic), len(table)))
        print()
        print('  %-12s %-8s %s' % ('BLOCK', 'ENTRIES', 'DISTINCT PADS'))
        for k in sorted(family, key=lambda k: -len(family[k])):
            pads = sorted({p for _, p, _ in family[k]})
            print('  %-12s %-8d %s' % (k, len(family[k]), pads))
        return

    print('%-22s %-6s %-5s %-5s %-5s %s' % ('SIGNAL', 'PORT', 'PIN', 'MUX', 'PAD', 'LINE'))
    for r in resolved:
        print('%-22s %-6s %-5s %-5s %-5s %s'
              % (r['signal'], r['port'], r['pin'], r['mux'],
                 '-' if r['pad'] is None else r['pad'], r['line']))


if __name__ == '__main__':
    main()
