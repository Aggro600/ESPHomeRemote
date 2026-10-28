import os,json,urllib.request,time,sys
tok=os.environ["SUPERVISOR_TOKEN"]
def call(path,data):
    r=urllib.request.Request("http://supervisor/core/api/"+path,data=json.dumps(data).encode(),headers={"Authorization":"Bearer "+tok,"Content-Type":"application/json"})
    return urllib.request.urlopen(r,timeout=30).read()
n=int(sys.argv[1]); ms=float(sys.argv[2]) if len(sys.argv)>2 else 0.5
call("services/scene/create",{"scene_id":"haengelampe_vor_blinken","snapshot_entities":["light.hangelampe"]})
for _ in range(n):
    call("services/light/turn_off",{"entity_id":"light.hangelampe","transition":0}); time.sleep(ms)
    call("services/light/turn_on",{"entity_id":"light.hangelampe","brightness_pct":100,"transition":0}); time.sleep(ms)
call("services/scene/turn_on",{"entity_id":"scene.haengelampe_vor_blinken","transition":0})
print("geblinkt",n)
