const fs=require('fs');
const h=fs.readFileSync('../C64Mesh/player.h','utf8');
const src=h.split('/*DECODE-START*/')[1].split('/*DECODE-END*/')[0];
const applyTiles=new Function(src+';return applyTiles;')();
const W=384,H=272,pal=[...Array(16).keys()];   // identity palette -> indices
const px=new Uint32Array(W*H);
for(const hex of JSON.parse(fs.readFileSync('tiles.json'))){const u8=Uint8Array.from(Buffer.from(hex,'hex'));const end=applyTiles(u8,px,pal,W);if(end!==u8.length)throw 'length mismatch'}
const ref=fs.readFileSync('ws_boot.raw');let diff=0;for(let i=0;i<W*H;i++)if(px[i]!==ref[i])diff++;
console.log('JS decoder vs Python decoder: '+diff+' differing pixels of '+W*H);
// full page script parses?
const s=h.split('<script>')[1].split('</script>')[0];new Function(s);console.log('player page JS parses ok');
const w=fs.readFileSync('../C64Mesh/webui.h','utf8');new Function(w.split('<script>')[1].split('</script>')[0]);console.log('settings page JS parses ok');
