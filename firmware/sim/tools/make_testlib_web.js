const fs=require('fs'),path=require('path');
(async()=>{ const files=await require('./testlib.js').build('small'); const out=path.join(__dirname,'../web/library'); fs.rmSync(out,{recursive:true,force:true}); fs.mkdirSync(out,{recursive:true});
 const man={files:[],credits:'Test library (made-up games).'};
 files.forEach((f,i)=>{ const url='f'+i+path.extname(f.path); fs.writeFileSync(path.join(out,url),Buffer.from(f.src),{flag:'w'}); man.files.push({path:f.path,url,size:f.size}); });
 fs.writeFileSync(path.join(out,'manifest.json'),JSON.stringify(man,null,1)); console.log(man.files.length,'files'); })();
