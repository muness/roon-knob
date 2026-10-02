#!/usr/bin/env python3
"""Build a stable chooser from one published generation of exact flash pages."""
import argparse
import html
import pathlib
import re

LABELS = {'dial':'HiPhi Dial (Waveshare)', 'frame':'HiPhi Frame', 'rlcd':'HiPhi RLCD',
          'joy':'HiPhi Joy (AtomS3 + Joystick)', 'tough':'HiPhi Tough (M5Stack)',
          'm5dial':'Dial Lab (M5Stack Dial)', 'sticks3':'Twist (M5StickS3)',
          'stopwatch':'Remote (M5Stack StopWatch)', 'stackchan':'Kizz (M5Stack StackChan)'}
PATTERN = re.compile(r'flash-([a-z0-9]+)-([a-f0-9]{40})-(\d+)-(\d+)\.html$')

def build(root, pr, date):
    pages = [(p, PATTERN.fullmatch(p.name)) for p in root.glob('flash-*.html')]
    pages = [(p,m) for p,m in pages if m and m[1] in LABELS]
    if not pages: raise ValueError('No exact firmware pages available')
    generation = max((int(m[3]),int(m[4])) for _,m in pages)
    pages = [(p,m) for p,m in pages if (int(m[3]),int(m[4])) == generation]
    if len({m[2] for _,m in pages}) != 1: raise ValueError('Mixed commits in build generation')
    first = pages[0][0].read_text()
    notice = re.search(r'<div class="channel-notice alpha">.*?</div>', first, re.S)
    if not notice: raise ValueError('Missing build provenance')
    cards=[]
    for target,label in LABELS.items():
        for p,m in pages:
            if m[1] == target:
                cards.append(f'<article class="flash-card"><div><h2>{html.escape(label)}</h2></div><div class="flash-action"><a class="nav-flash" href="./{p.name}">Flash {html.escape(label)}</a></div></article>')
    return f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>PR #{pr} firmware · HiPhi</title><link rel="stylesheet" href="./assets/styles.css"></head>
<body class="flash-shell"><header class="site-header"><div class="container nav-wrap"><a class="brand" href="/">HiPhi firmware</a></div></header><main class="flash-main"><div class="container"><div class="flash-intro"><div><h1>PR #{pr} firmware</h1><p>Choose your device to flash the latest published preview.</p><p>Preview version: pr-{pr} · Build date: {html.escape(date)}</p></div>{notice[0]}</div><div class="flash-list">{''.join(cards)}</div><p>Dial auxiliary-chip instructions are included on the Dial flash page.</p></div></main></body></html>'''

if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('root',type=pathlib.Path);parser.add_argument('--pr',type=int,required=True);parser.add_argument('--date',required=True)
    args=parser.parse_args();(args.root/'index.html').write_text(build(args.root,args.pr,args.date))
