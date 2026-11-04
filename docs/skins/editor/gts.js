// GTi skin editor: the .gts container and the starter atlas.
// COPIED word for word from firmware/shared/webui.html (the panel page): the block GTS-BEGIN..GTS-END and gtsDrawStarter
// (STARTER-BEGIN..STARTER-END). Change one, change the other. tools/test_skin_editor.js checks that they are identical and that
// both pack the same bytes. No DOM in the GTS block (Node runs it); gtsDrawStarter only needs a 2D canvas context.
// GTS-BEGIN
// The container of firmware/shared/skin_gts.h. No DOM: tools/gts_fixture.js runs this block in Node.
const GTS_ATLAS_W=256,GTS_ATLAS_H=192,GTS_SLOT_VER=1,GTS_BG={1:[480,320],2:[320,480],3:[480,272],4:[272,480]};
// = skin::SLOTS in firmware/shared/skin_draw.h (slot list 1): name x y w h border(9-slice)
const GTS_SLOTS='KEY_UP 0 0 48 48 10,KEY_DOWN 48 0 48 48 10,PANEL 96 0 48 48 10,ROW_SEL 144 0 48 32 8,TILE 0 64 64 64 0,ICONS 64 64 128 32 0,ICONS_2X 0 128 256 64 0'
  .split(',').map(t=>{const[n,x,y,w,h,b]=t.split(' ');return{n,x:+x,y:+y,w:+w,h:+h,b:+b};});
function gtsErr(t){throw new Error(t);}
function gtsIsPng(b){return b.length>7&&[137,80,78,71,13,10,26,10].every((v,i)=>b[i]==v);}
function gtsIsJpeg(b){return b.length>2&&b[0]==255&&b[1]==216&&b[2]==255;}
function gtsPack(man,atlas,bgs=[]){
  const m=new TextEncoder().encode(JSON.stringify(man)),n=bgs.reduce((t,g)=>t+5+g.bytes.length,13+m.length+atlas.length);
  if(m.length>1024)gtsErr('manifest is '+m.length+' bytes, the limit is 1024');
  if(!gtsIsPng(atlas))gtsErr('atlas is not a PNG');
  if(bgs.length>4)gtsErr('too many backgrounds');
  bgs.forEach((g,i)=>{if(!GTS_BG[g.id])gtsErr('unknown background id');if(bgs.findIndex(o=>o.id==g.id)<i)gtsErr('duplicate background id');
    if(!gtsIsJpeg(g.bytes))gtsErr('background is not a JPEG');});
  if(n>262144)gtsErr('skin is '+n+' bytes, the limit is 262144');
  const o=new Uint8Array(n),v=new DataView(o.buffer);let p=8+m.length;
  o.set([71,84,83,49,0,GTS_SLOT_VER]);v.setUint16(6,m.length,true);o.set(m,8);
  v.setUint32(p,atlas.length,true);o.set(atlas,p+4);p+=4+atlas.length;o[p++]=bgs.length;
  for(const g of bgs){o[p]=g.id;v.setUint32(p+1,g.bytes.length,true);o.set(g.bytes,p+5);p+=5+g.bytes.length;}
  return o;
}
// gts::parse with gts::errText's words; also refuses a slot list this page does not know.
function gtsUnpack(buf){
  const d=new Uint8Array(buf),n=d.length,L=()=>gtsErr('bad chunk length'),bg=[];
  if(n>262144)gtsErr('file too big');
  if(n<8)gtsErr('file too short');
  if(String.fromCharCode(d[0],d[1],d[2],d[3])!='GTS1')gtsErr('not a GTS1 skin file');
  if(d[5]!=GTS_SLOT_VER)gtsErr('unsupported slot version');
  const v=new DataView(d.buffer,d.byteOffset,n),ml=v.getUint16(6,true);let p=8,man=null;
  if(ml>1024||ml>n-p)L();
  const mb=d.subarray(p,p+=ml);
  if(n-p<4)L();
  const al=v.getUint32(p,true);p+=4;
  if(al>n-p)L();
  const atlas=d.slice(p,p+=al);
  if(!gtsIsPng(atlas))gtsErr('chunk is not a PNG');
  if(n-p<1)L();
  const nb=d[p++];
  if(nb>6)gtsErr('too many backgrounds');
  for(let i=0;i<nb;i++){
    if(n-p<5)L();
    const id=d[p],len=v.getUint32(p+1,true);p+=5;
    if(!GTS_BG[id])gtsErr('unknown background id');
    if(bg.some(g=>g.id==id))gtsErr('duplicate background id');
    if(len>n-p)L();
    const b=d.slice(p,p+=len);
    if(!gtsIsJpeg(b))gtsErr('background is not a JPEG');
    bg.push({id,bytes:b});
  }
  if(p!=n)L();
  try{man=JSON.parse(new TextDecoder().decode(mb));}catch(e){}
  if(!man||typeof man!='object')gtsErr('manifest is not JSON');
  return{manifest:man,atlas,bg};
}
function gtsCheckAtlas(w,h){
  return w!=GTS_ATLAS_W?'atlas is '+w+' px wide, expected '+GTS_ATLAS_W:h!=GTS_ATLAS_H?'atlas is '+h+' px high, expected '+GTS_ATLAS_H:null;
}
function gtsCheckBg(id,w,h){
  const s=GTS_BG[id];
  return !s?'unknown background id '+id:w==s[0]&&h==s[1]?null:'background is '+w+'x'+h+', expected '+s[0]+'x'+s[1];
}
// PNG IHDR / JPEG SOFn, read before sending (the panel takes 8-bit RGBA non-interlaced PNG and baseline JPEG). null = unreadable.
// reencode: the first SOFn is not SOF0 (progressive, extended, lossless, arithmetic): the panel only decodes baseline.
function gtsPngInfo(b){
  if(!gtsIsPng(b)||b.length<29||String.fromCharCode(b[12],b[13],b[14],b[15])!='IHDR')return null;
  const v=new DataView(b.buffer,b.byteOffset,b.length);
  return{w:v.getUint32(16),h:v.getUint32(20),depth:b[24],type:b[25],interlace:b[28]};
}
function gtsJpegInfo(b){
  if(!gtsIsJpeg(b))return null;
  for(let p=2;p+9<b.length;){
    const m=b[p+1];
    if(b[p]!=255||m==0xD9||m==0xDA)return null;   // lost, or image data before any SOFn
    if(m==255||m==1||(m>=0xD0&&m<=0xD8)){p+=m==255?1:2;continue;}
    if(m>=0xC0&&m<=0xCF&&m!=0xC4&&m!=0xC8&&m!=0xCC)return{w:b[p+7]<<8|b[p+8],h:b[p+5]<<8|b[p+6],reencode:m!=0xC0};
    p+=2+(b[p+2]<<8|b[p+3]);
  }
  return null;
}
// GTS-END
// STARTER-BEGIN
function gtsDrawStarter(c,labelled){
  const S={},A=196/255,cells=(s,k,f)=>{for(let i=0;i<s.w*s.h/k/k;i++)f(s.x+i%(s.w/k)*k,s.y+(i/(s.w/k)|0)*k,i);};
  GTS_SLOTS.forEach(s=>S[s.n]=s);c.clearRect(0,0,256,192);c.lineWidth=1;
  const box=(s,i)=>c.strokeRect(s.x+i+.5,s.y+i+.5,s.w-2*i-1,s.h-2*i-1);
  if(labelled){
    c.fillStyle='#808080';c.fillRect(0,0,256,192);c.font='8px sans-serif';c.textBaseline='top';
    GTS_SLOTS.forEach((s,i)=>{c.fillStyle=`hsl(${i*51},45%,32%)`;c.fillRect(s.x,s.y,s.w,s.h);c.strokeStyle='#fff';box(s,0);
      if(s.b){c.setLineDash([2,2]);box(s,s.b);c.setLineDash([]);}});
    c.strokeStyle='#fff6';cells(S.ICONS,16,(x,y)=>box({x,y,w:16,h:16},0));cells(S.ICONS_2X,32,(x,y)=>box({x,y,w:32,h:32},0));
    c.fillStyle='#fff';GTS_SLOTS.forEach(s=>c.fillText(s.n,s.x+2,s.y+2));return;
  }
  [['KEY_UP',0,8],['KEY_DOWN',1,8],['PANEL',0,8],['ROW_SEL',1,4]].forEach(([n,on,r])=>{const s=S[n],x=s.x+.5,y=s.y+.5,w=s.w-1,h=s.h-1;
    c.beginPath();c.moveTo(x+r,y);c.arcTo(x+w,y,x+w,y+h,r);c.arcTo(x+w,y+h,x,y+h,r);c.arcTo(x,y+h,x,y,r);c.arcTo(x,y,x+w,y,r);c.closePath();
    c.fillStyle=on?`rgba(57,44,16,${A})`:`rgba(16,24,49,${A})`;c.fill();c.strokeStyle=on?'#f7b231':'#42598c';c.stroke();});   // COL_SEL/amber, COL_PANEL/COL_SEP
  const t=S.TILE;c.fillStyle='#171333';c.fillRect(t.x,t.y,t.w,t.h);c.fillStyle='#21234a';
  [[0,21,64,1],[40,21,1,30],[9,50,32,1],[52,0,1,12],[52,11,12,1]].forEach(r=>c.fillRect(t.x+r[0],t.y+r[1],r[2],r[3]));
  c.strokeStyle='#282c58';c.beginPath();c.arc(t.x+9.5,t.y+50.5,3,0,7);c.stroke();
  const P=(...a)=>{c.beginPath();for(let i=0;i<a.length;i+=2)a[i]<0?(c.moveTo(a[i+1],a[i+2]),i++):c.lineTo(a[i],a[i+1]);},
    O=(x,y,r,a=0,b=7)=>{c.beginPath();c.arc(x,y,r,a,b);c.stroke();};
  const icon=[   // INSERT EJECT FOLDER STAR GEAR BATTERY WIFI SEARCH (16x16 grid)
    ()=>{P(8,2,8,10,-1,4,7,8,11,12,7,-1,3,14,13,14);c.stroke();},
    ()=>{P(3,10,8,4,13,10);c.fill();c.fillRect(3,12,10,2);},
    ()=>{P(2,4,6,4,8,6,14,6,14,13,2,13,2,4);c.stroke();},
    ()=>{c.beginPath();for(let i=0;i<10;i++){const r=i&1?2.6:6.5,a=i*.628-1.571;c.lineTo(8+r*Math.cos(a),8.5+r*Math.sin(a));}c.fill();},
    ()=>{O(8,8,3);c.lineWidth=3;c.setLineDash([1.9,1.5]);O(8,8,5.5);c.setLineDash([]);},
    ()=>{c.strokeRect(2,5,11,7);c.fillRect(13,7,2,3);c.fillRect(4,7,6,3);},
    ()=>{[3,6.5,10].forEach(r=>O(8,13,r,-2.4,-.74));c.fillRect(7,12,2,2);},
    ()=>{O(7,7,4);P(10,10,14,14);c.stroke();}];
  c.fillStyle=c.strokeStyle='#eff3ff';
  [[S.ICONS,1],[S.ICONS_2X,2]].forEach(([s,k])=>cells(s,16*k,(x,y,i)=>{if(icon[i]){c.save();c.translate(x,y);c.scale(k,k);c.lineWidth=1.5;icon[i]();c.restore();}}));
}
// STARTER-END

// ---- Editor only (not in the panel page as such; tools/test_skin_editor.js checks them against it) ----------------------------
// The colour presets of the panel's Theme Editor (TE_PRESETS in firmware/shared/webui.html); NEO is the starter's.
const TE_PRESETS = {
  NEO:         { fill: '#101831', light: '#42598C', dark: '#080C19', text: '#EFF3FF', accent: '#F7B231', bevel: 'flat',   bevelW: 1, radius: 10, scale: 2 },
  AMIGA_WB2:   { fill: '#a8a8a8', light: '#ffffff', dark: '#000000', text: '#000000', accent: '#5a6ea0', bevel: 'raised', bevelW: 2, radius: 0, scale: 2 },
  AMIGA_WB13:  { fill: '#0055aa', light: '#ffffff', dark: '#000044', text: '#ffffff', accent: '#ff8800', bevel: 'raised', bevelW: 2, radius: 0, scale: 2 },
  PAPER_WHITE: { fill: '#eeeeea', light: '#ffffff', dark: '#787874', text: '#141414', accent: '#285abe', bevel: 'raised', bevelW: 2, radius: 3, scale: 2 },
  MIDNIGHT:    { fill: '#222630', light: '#485062', dark: '#0c0e14', text: '#dee4f0', accent: '#5aaaff', bevel: 'flat',   bevelW: 1, radius: 4, scale: 2 },
  PHOSPHOR:    { fill: '#08180c', light: '#28c85a', dark: '#040c06', text: '#50ff82', accent: '#50ff82', bevel: 'flat',   bevelW: 1, radius: 0, scale: 2 },
  OMEGA_DARK:  { fill: '#181c31', light: '#3b4570', dark: '#0b0e22', text: '#e9ecf5', accent: '#3fe0e8', bevel: 'flat',   bevelW: 1, radius: 10, scale: 2 },
};

// teNormalizeStyle of the panel page: a style from any source; anything missing falls back to AMIGA_WB2, numbers clamp.
function teNormalizeStyle(s) {
  const d = TE_PRESETS.AMIGA_WB2;
  const hex = (v, f) => (typeof v === 'string' && /^#[0-9a-fA-F]{6}$/.test(v)) ? v.toLowerCase() : f;
  const num = (v, f, lo, hi) => { const n = parseInt(v); return Number.isFinite(n) ? Math.max(lo, Math.min(hi, n)) : f; };
  return {
    fill: hex(s && s.fill, d.fill), light: hex(s && s.light, d.light), dark: hex(s && s.dark, d.dark), text: hex(s && s.text, d.text),
    accent: hex(s && s.accent, d.accent),
    bevel: ['raised', 'flat', 'none'].includes(s && s.bevel) ? s.bevel : d.bevel,
    bevelW: num(s && s.bevelW, d.bevelW, 0, 6), radius: num(s && s.radius, d.radius, 0, 12), scale: num(s && s.scale, d.scale, 1, 4),
  };
}
// What the panel page packs (skinSend): Object.assign({author:''}, man, teStyle(), {name}). The same key order, so the same bytes.
function skinManifest(man, style, name, author) {
  return Object.assign({ author: author || '' }, man, style, { name, author: author || '' });
}
