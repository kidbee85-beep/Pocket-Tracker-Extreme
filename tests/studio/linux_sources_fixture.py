#!/usr/bin/env python3
"""Local reproducible fixture; never substitutes invented tracks into production."""
import http.server,json,math,os,pathlib,shutil,socketserver,struct,subprocess,sys,tempfile,threading,time,wave

def main():
    executable=pathlib.Path(sys.argv[1]).resolve()
    directory=pathlib.Path(sys.argv[2]).resolve();directory.mkdir(parents=True,exist_ok=True)
    sample=directory/'fixture.wav'
    with wave.open(str(sample),'wb') as f:
        f.setnchannels(2);f.setsampwidth(2);f.setframerate(44100)
        f.writeframes(b''.join(struct.pack('<hh',int(16000*math.sin(i*2*math.pi*440/44100)),int(12000*math.sin(i*2*math.pi*660/44100))) for i in range(132300)))
    ffmpeg=os.environ.get('PT_SOURCE_FIXTURE_FFMPEG',shutil.which('ffmpeg') or '/Applications/Krita.app/Contents/MacOS/ffmpeg')
    codecs=[]
    if pathlib.Path(ffmpeg).is_file():
        encoders=subprocess.check_output([ffmpeg,'-hide_banner','-encoders'],stderr=subprocess.DEVNULL,text=True)
        vorbis=['-codec:a','libvorbis'] if 'libvorbis' in encoders else ['-codec:a','vorbis','-strict','-2']
        codec_args={'mp3':['-codec:a','libmp3lame'],'flac':['-codec:a','flac'],'ogg':vorbis,'opus':['-codec:a','libopus'],'m4a':['-codec:a','aac'],'m4a-frag':['-codec:a','aac','-movflags','frag_keyframe+empty_moov+default_base_moof','-f','mp4']}
        for ext,args in codec_args.items():
            subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-nostdin','-y','-i',str(sample),*args,str(directory/f'fixture.{ext}')],check=True)
            codecs.append(ext)
    class Server(socketserver.ThreadingMixIn,http.server.HTTPServer): daemon_threads=True
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self,*args): pass
        def do_GET(self):
            base=f'http://127.0.0.1:{self.server.server_port}'
            if self.path.startswith('/redirect-bad'):
                self.send_response(302);self.send_header('Location','https://example.invalid/blocked.wav');self.end_headers();return
            if self.path.startswith('/slow.wav'):
                self.send_response(200);self.send_header('Content-Length','1000000');self.end_headers()
                try:
                    for _ in range(100): self.wfile.write(b'x'*100);self.wfile.flush();time.sleep(.1)
                except (BrokenPipeError,ConnectionResetError): pass
                return
            if self.path.startswith('/broken.wav'): data=b'not a RIFF WAV'
            elif self.path.startswith('/codec/'): data=(directory/pathlib.Path(self.path).name).read_bytes()
            elif self.path.startswith('/archive/advancedsearch.php'):
                data=json.dumps({'response':{'numFound':2,'docs':[{'identifier':'fixture-b','title':'B album','description':'<b>Real fixture metadata</b>','creator':['Fixture Artist'],'subject':['Jazz']},{'identifier':'fixture-a','title':'A album'}]}}).encode()
            elif self.path.startswith('/archive/metadata/'):
                if self.path.endswith('fixture-a'): data=json.dumps({'metadata':{},'files':[]}).encode()
                else:data=json.dumps({'metadata':{'creator':'Fixture Artist','description':'Real fixture metadata','instrumentation':'synth test signal'},'files':[{'name':'b.wav','title':'B tone','length':'3.0'},{'name':'a.wav','title':'A tone','length':'3.0'}]}).encode()
            elif self.path.startswith('/commons/api.php'):
                if 'fail-query' in self.path:data=json.dumps({'error':{'info':'Deliberate fixture failure'}}).encode()
                else:data=json.dumps({'query':{'pages':[{'title':'File:Tone.wav','imageinfo':[{'mime':'audio/wav','mediatype':'AUDIO','url':base+'/tone.wav','descriptionurl':'https://commons.wikimedia.org/wiki/File:Tone.wav','extmetadata':{'ObjectName':{'value':'Commons Tone'},'Artist':{'value':'<i>Fixture Artist</i>'},'LicenseUrl':{'value':'https://creativecommons.org/publicdomain/zero/1.0/'}}}]}]}}).encode()
            else: data=sample.read_bytes()
            self.send_response(200);self.send_header('Content-Length',str(len(data)));self.end_headers()
            try:self.wfile.write(data)
            except (BrokenPipeError,ConnectionResetError):pass
    server=Server(('127.0.0.1',0),Handler);threading.Thread(target=server.serve_forever,daemon=True).start()
    base=f'http://127.0.0.1:{server.server_port}'
    helper=directory/'yt-dlp'
    helper.write_text('#!/usr/bin/env python3\nimport json,sys,pathlib,os,time\n'+f'base={base!r}\nsample={str(sample)!r}\nheartbeat={str(directory/"helper-heartbeat")!r}\n'+'''if any('slow-helper' in a for a in sys.argv):
 if os.fork()==0:
  while True:
   with open(heartbeat,'a') as f:f.write('alive\\n')
   time.sleep(.02)
 while True:time.sleep(.1)
elif '-o' in sys.argv:
 output=pathlib.Path(sys.argv[sys.argv.index('-o')+1])
 if 'fixture-too-large' in sys.argv:
  with output.open('wb') as f:f.truncate(97*1024*1024)
 else:output.write_bytes(pathlib.Path(sample).read_bytes())
elif any(a.startswith('ytsearch') for a in sys.argv):
 identifier='fixtureBIG1' if any('too-large' in a for a in sys.argv) else 'fixtureABC1'
 print(json.dumps({'entries':[{'id':identifier,'title':'YouTube Fixture','duration':3,'channel':'Fixture Artist'}]}))
else:
 fmt='fixture-too-large' if any('fixtureBIG1' in a for a in sys.argv) else 'fixture-audio-only'
 print(json.dumps({'id':'fixtureABC1','url':base+'/tone.wav','ext':'wav','duration':3,'protocol':'https','vcodec':'none','format_id':fmt}))
''')
    helper.chmod(0o755)
    env=os.environ.copy();env.update(PT_SOURCE_TEST_BASE=base,PT_YTDLP=str(helper),PT_SOURCE_CODEC_LIST=','.join(codecs),PT_FFMPEG=ffmpeg)
    try: result=subprocess.run([str(executable),str(directory/'data'),str(sample)],env=env,timeout=50);return result.returncode
    finally:server.shutdown();server.server_close()
if __name__=='__main__':sys.exit(main())
