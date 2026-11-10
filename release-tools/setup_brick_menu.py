#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Add/update only TrackWerk's KNULLI menu entry. Plan by default; retain other ports."""
from pathlib import Path
import argparse, hashlib, json, os, tempfile, xml.etree.ElementTree as ET

IMAGE='menu-cover-trackwerk-0.9.png'
LAUNCHERS=('TrackWerk.sh','Pocket Tracker Extreme.sh')

def digest(data):return hashlib.sha256(data).hexdigest()
def checked(path):
    p=Path(os.path.abspath(path))
    if any(q.is_symlink() for q in [p,*p.parents]):raise RuntimeError('Symbolic link retained; automatic menu update refused')
    return p
def parse(data):return ET.fromstring(data,parser=ET.XMLParser(target=ET.TreeBuilder(insert_comments=True)))
def gamepath(g):return g.findtext('path','').replace('\\','/').removeprefix('./')

def update(target,art=None,apply=False,launcher_name=None):
    target=checked(target);ports=target.parent;listing=checked(ports/'gamelist.xml')
    if target.name!='pockettracker_extreme' or not target.is_dir() or not (target/'pockettracker_extreme.aarch64').is_file():raise RuntimeError('Select the installed pockettracker_extreme app directory')
    original=listing.read_bytes() if listing.exists() else None
    tree=parse(original) if original is not None else ET.Element('gameList')
    if tree.tag!='gameList':raise RuntimeError('Unexpected gamelist root; retained')
    if launcher_name and launcher_name not in LAUNCHERS:raise RuntimeError('Unknown launcher filename')
    matches=[g for g in tree.findall('game') if gamepath(g) in LAUNCHERS and (not launcher_name or gamepath(g)==launcher_name)]
    if len(matches)>1:raise RuntimeError('More than one tracker entry; use --launcher to select the existing path')
    if matches:
        game=matches[0];name=gamepath(game)
    else:
        names=[launcher_name] if launcher_name else list(LAUNCHERS)
        name=next((n for n in names if (ports/n).is_file()),None)
        if name is None:raise RuntimeError('Existing TrackWerk launcher not found')
        game=ET.SubElement(tree,'game');ET.SubElement(game,'path').text='./'+name
    launcher=checked(ports/name);launcher_sha=digest(launcher.read_bytes());oldpath=game.findtext('path')
    before_others=[ET.tostring(g) for g in tree.findall('game') if g is not game]
    picture=checked(art or target/IMAGE).read_bytes()
    if picture[:8]!=b'\x89PNG\r\n\x1a\n':raise RuntimeError('Expected supplied PNG menu artwork')
    dest=checked(target/IMAGE)
    if dest.exists() and dest.read_bytes()!=picture:raise RuntimeError('Different existing artwork retained')
    values={'name':'TrackWerk 0.9','image':'./pockettracker_extreme/'+IMAGE,'thumbnail':'./pockettracker_extreme/'+IMAGE}
    already=bool(matches) and dest.is_file() and all(game.findtext(k)==v for k,v in values.items())
    for tag,value in values.items():
        child=game.find(tag)
        if child is None:child=ET.SubElement(game,tag)
        child.text=value
    assert game.findtext('path')==oldpath
    result={'status':'ALREADY_CURRENT' if already else 'PLAN_ONLY','launcher':name,'launcher_sha256':launcher_sha,'new_entry':not bool(matches),'other_entries_retained':len(before_others),'art_sha256':digest(picture),'apply':apply}
    if not apply or already:return result
    backups=checked(target/'.pte-program-backups/menu');backups.mkdir(parents=True,exist_ok=True)
    if original is not None:
        backup=backups/(digest(original)+'.gamelist.xml')
        if backup.exists():
            if backup.read_bytes()!=original:raise RuntimeError('Existing XML backup differs')
        else:
            with backup.open('xb') as f:f.write(original);f.flush();os.fsync(f.fileno())
        result['backup']=str(backup)
    if not dest.exists():
        with dest.open('xb') as f:f.write(picture);f.flush();os.fsync(f.fileno())
    data=ET.tostring(tree,encoding='utf-8',xml_declaration=True)
    if original is None or original.endswith(b'\n'):data+=b'\n'
    temp=None
    try:
        with tempfile.NamedTemporaryFile(prefix='.trackwerk-menu-',dir=ports,delete=False) as f:
            temp=Path(f.name);f.write(data);f.flush();os.fsync(f.fileno())
        current=listing.read_bytes() if listing.exists() else None
        if current!=original:raise RuntimeError('Menu changed concurrently; retained')
        os.replace(temp,listing);temp=None
    finally:
        if temp and temp.exists():temp.unlink()
    actual=parse(listing.read_bytes());entry=next(g for g in actual.findall('game') if g.findtext('path')==oldpath)
    assert all(entry.findtext(k)==v for k,v in values.items())
    assert [ET.tostring(g) for g in actual.findall('game') if g is not entry]==before_others
    assert digest(launcher.read_bytes())==launcher_sha
    result.update(status='MENU_UPDATED_WITH_BACKUP',new_xml_sha256=digest(listing.read_bytes()));return result

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--target',required=True,type=Path);p.add_argument('--art',type=Path);p.add_argument('--apply',action='store_true');p.add_argument('--launcher',choices=LAUNCHERS);a=p.parse_args()
    try:print(json.dumps(update(a.target,a.art,a.apply,a.launcher),indent=2))
    except (OSError,RuntimeError,ValueError,ET.ParseError) as e:p.exit(1,'Menu update stopped: '+str(e)+'\n')
