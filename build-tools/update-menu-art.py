#!/usr/bin/env python3
"""Update only TrackWerk's existing Ports display entry, with a byte-exact XML backup."""
from pathlib import Path
import argparse,hashlib,json,os,tempfile,xml.etree.ElementTree as ET
IMAGE='menu-cover-trackwerk-0.9.png'
LAUNCHER='Pocket Tracker Extreme.sh'
def digest(blob):return hashlib.sha256(blob).hexdigest()
def entry_path(game):
 value=game.findtext('path','').replace('\\','/')
 return value[2:] if value.startswith('./') else value
def update(target,art,apply=False):
 target=Path(target).absolute();art=Path(art).absolute();ports=target.parent;listing=ports/'gamelist.xml';launcher=ports/LAUNCHER
 for p in [target,listing,launcher,art]:
  if p.is_symlink() or any(q.is_symlink() for q in p.parents):raise RuntimeError('Symbolic link retained; automatic menu update refused')
 if target.name!='pockettracker_extreme' or not target.is_dir() or not (target/'data').is_dir() or not launcher.is_file():raise RuntimeError('Existing tracker/launcher not found')
 original=listing.read_bytes();picture=art.read_bytes()
 if picture[:8]!=b'\x89PNG\r\n\x1a\n':raise RuntimeError('Expected the supplied PNG artwork')
 tree=ET.fromstring(original,parser=ET.XMLParser(target=ET.TreeBuilder(insert_comments=True)))
 matches=[g for g in tree.findall('game') if entry_path(g)==LAUNCHER]
 if len(matches)!=1:raise RuntimeError('Expected exactly one existing tracker menu entry')
 game=matches[0];oldpath=game.findtext('path');before_others=[ET.tostring(g) for g in tree.findall('game') if g is not game];before_launcher=digest(launcher.read_bytes())
 image_path='./pockettracker_extreme/'+IMAGE
 for tag,value in [('name','TrackWerk 0.9'),('image',image_path),('thumbnail',image_path)]:
  element=game.find(tag)
  if element is None:element=ET.SubElement(game,tag)
  element.text=value
 assert game.findtext('path')==oldpath
 assert [ET.tostring(g) for g in tree.findall('game') if g is not game]==before_others
 destination=target/IMAGE
 if destination.exists() and destination.read_bytes()!=picture:raise RuntimeError('Different existing artwork retained; no replacement attempted')
 current=ET.fromstring(original)
 current_game=[g for g in current.findall('game') if entry_path(g)==LAUNCHER][0]
 already=all(current_game.findtext(k)==v for k,v in [('name','TrackWerk 0.9'),('image',image_path),('thumbnail',image_path)]) and destination.is_file()
 report=dict(status='ALREADY_CURRENT' if already else 'PLANNED',target=str(target),only_menu_metadata_and_new_art=True,old_xml_sha256=digest(original),launcher_sha256=before_launcher,art_sha256=digest(picture),apply=apply)
 if not apply or already:return report
 backupdir=target/'.pte-program-backups/menu';backupdir.mkdir(parents=True,exist_ok=True);backup=backupdir/(digest(original)+'.gamelist.xml')
 if backup.exists():
  if backup.read_bytes()!=original:raise RuntimeError('Backup conflict retained')
 else:
  with backup.open('xb') as f:f.write(original);f.flush();os.fsync(f.fileno())
 if not destination.exists():
  with destination.open('xb') as f:f.write(picture);f.flush();os.fsync(f.fileno())
 assert destination.read_bytes()==picture and backup.read_bytes()==original
 temp=None
 try:
  with tempfile.NamedTemporaryFile(prefix='.trackwerk-menu-',dir=ports,delete=False) as f:
   temp=Path(f.name);f.write(ET.tostring(tree,encoding='utf-8',xml_declaration=True));f.flush();os.fsync(f.fileno())
  if listing.read_bytes()!=original:raise RuntimeError('Menu changed concurrently; original retained')
  os.replace(temp,listing);temp=None
 finally:
  if temp and temp.exists():temp.unlink()
 actual=ET.fromstring(listing.read_bytes(),parser=ET.XMLParser(target=ET.TreeBuilder(insert_comments=True)))
 other=[g for g in actual.findall('game') if g.findtext('path')!=oldpath]
 assert [ET.tostring(g) for g in other]==before_others and digest(launcher.read_bytes())==before_launcher
 report.update(status='MENU_UPDATED_WITH_BACKUP',backup=str(backup),new_xml_sha256=digest(listing.read_bytes()))
 return report
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--target',required=True,type=Path);p.add_argument('--art',required=True,type=Path);p.add_argument('--apply',action='store_true');a=p.parse_args()
 print(json.dumps(update(a.target,a.art,a.apply),indent=2))
