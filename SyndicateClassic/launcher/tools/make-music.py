#!/usr/bin/env python3
# Turns songs into launcher tracks: python make-music.py "01 - Menu Theme.mp3" "02 - Something.mp3" ...
# Needs ffmpeg on PATH. Writes <name>.tadp next to each input; put them in launcher/launcher-source/music/.
# Format (read by SyndicateClassic/MenuMusicPlayer.cs): "TAD1", rate u32 (32000), channels u16 (2), block frames u32,
# frames u32, then per block: per channel int16 predictor + byte step index + byte 0, then one byte per frame
# (left = low 4 bits, right = high 4 bits), IMA ADPCM. All tracks must be 32 kHz stereo.
import array, os, struct, subprocess, sys
STEP=[7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767]
MOVE=[-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8]
RATE, BLOCK = 32000, 4096
def convert(src):
    raw = subprocess.run(['ffmpeg','-v','error','-i',src,'-ac','2','-ar',str(RATE),'-f','s16le','-'],check=True,capture_output=True).stdout
    pcm = array.array('h'); pcm.frombytes(raw[:len(raw)//4*4]); frames = len(pcm)//2
    out = bytearray(b'TAD1'+struct.pack('<IHII',RATE,2,BLOCK,frames)); pred=[0,0]; idx=[0,0]
    def enc(c,s):
        p=pred[c]; i=idx[c]; step=STEP[i]; diff=s-p; nib=0
        if diff<0: nib=8; diff=-diff
        delta=step>>3
        if diff>=step: nib|=4; diff-=step; delta+=step
        step>>=1
        if diff>=step: nib|=2; diff-=step; delta+=step
        step>>=1
        if diff>=step: nib|=1; delta+=step
        p = p-delta if nib&8 else p+delta
        pred[c]=max(-32768,min(32767,p)); idx[c]=max(0,min(88,i+MOVE[nib])); return nib
    for b in range(0,frames,BLOCK):
        for c in (0,1): pred[c]=pcm[2*b+c]; out+=struct.pack('<hBB',pred[c],idx[c],0)
        for f in range(min(BLOCK,frames-b)): out.append(enc(0,pcm[2*(b+f)])|(enc(1,pcm[2*(b+f)+1])<<4))
    dst = os.path.splitext(src)[0]+'.tadp'; open(dst,'wb').write(out); print(dst, len(out)//1024, 'KB', round(frames/RATE), 's')
for path in sys.argv[1:]: convert(path)
