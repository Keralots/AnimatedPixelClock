(function () {
'use strict';
var $ = function (id) { return document.getElementById(id); };
var converted = null, storage = null, busy = false;
function message(text) { $('gifMessage').textContent = text; }
async function request(path, body) {
 var response = await fetch(path, body ? {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)} : {});
 var data = await response.json();
 if (!response.ok || data.error) throw new Error(data.error || data.message || 'Request failed');
 return data;
}
function invalidate() { converted=null; $('gifUpload').disabled=true; $('gifPreview').removeAttribute('src'); }
function setBusy(value) { busy=value; ['gifConvert','gifRefresh','gifPlay','gifDelete'].forEach(function(id){$(id).disabled=value;});$('gifUpload').disabled=value||!converted; }
async function refresh() {
 storage = await request('/api/animations/storage');
 var budget=storage.maxUploadBytes===undefined?Math.max(0,storage.free-16384):storage.maxUploadBytes;
 $('gifBudget').textContent=storage.device+' | '+Math.floor(storage.free/1024)+' KiB free | '+Math.floor(budget/1024)+' KiB upload budget';
 $('gifFiles').innerHTML='';
 (storage.anims||[]).forEach(function(a){var o=document.createElement('option');o.value=a.name;o.textContent=a.name+' ('+a.frames+' frames)';$('gifFiles').appendChild(o);});
 if(!storage.usable)throw new Error('Animation storage is disabled. Update device firmware to enable short clips on 4MB boards.');
 return budget;
}
async function run(action) { if(busy)return;setBusy(true);try{await action();}catch(e){message(e.message);}finally{setBusy(false);} }
function readBase64(file) { return new Promise(function(resolve,reject){var reader=new FileReader();reader.onload=function(){resolve(reader.result.split(',')[1]);};reader.onerror=function(){reject(new Error('Cannot read GIF'));};reader.readAsDataURL(file);}); }
$('gifRefresh').onclick=function(){run(async function(){await refresh();message('Storage refreshed.');});};
['gifFit','gifAnchor','gifColors','gifSkip','gifAuto','gifInput'].forEach(function(id){$(id).addEventListener('change',invalidate);});
$('gifInput').addEventListener('change',function(){var f=this.files[0];if(f)$('gifName').value=f.name.replace(/\.gif$/i,'').replace(/[^A-Za-z0-9_-]/g,'').slice(0,24)||'animation';});
$('gifConvert').onclick=function(){run(async function(){
 invalidate();var file=$('gifInput').files[0];if(!file)throw new Error('Choose a GIF first.');if(file.size>8*1024*1024)throw new Error('GIF input limit is 8MiB.');
 message('Reading device storage and preparing preview...');var budget=await refresh();
 converted=await request('/api/animations/convert',{gif:await readBase64(file),fit:$('gifFit').value,anchor:$('gifAnchor').value,colors:Number($('gifColors').value),frameSkip:Number($('gifSkip').value),autoFit:$('gifAuto').checked,maxBytes:budget});
 $('gifPreview').src='data:image/gif;base64,'+converted.preview;
 message(converted.frames+' / '+converted.sourceFrames+' frames, '+converted.seconds.toFixed(1)+'s, '+(converted.bytes/1024).toFixed(1)+' KiB. Kept every '+converted.frameSkip+' frame(s). Review the preview, then upload.');
});};
$('gifUpload').onclick=function(){run(async function(){
 if(!converted)throw new Error('Create a preview first.');var name=$('gifName').value;
 if(storage&&(storage.anims||[]).some(function(a){return a.name===name;})&&!window.confirm('Replace '+name+' on the device?'))return;
 message('Uploading...');await request('/api/animations/upload',{name:name,pca:converted.pca});await refresh();$('gifFiles').value=name;message('Uploaded. Click Play selected to start it.');
});};
$('gifPlay').onclick=function(){run(async function(){var name=$('gifFiles').value;if(!name)throw new Error('Select an uploaded animation.');await request('/api/animations/play',{name:name});message('Playing '+name+'. Playback selection is temporary until saved in the device portal.');});};
$('gifDelete').onclick=function(){run(async function(){var name=$('gifFiles').value;if(!name)throw new Error('Select an animation.');if(!window.confirm('Delete '+name+' from the device?'))return;await request('/api/animations/delete',{name:name});await refresh();message('Deleted '+name+'.');});};
})();
