#!/usr/bin/env python3
import argparse,csv,os,sys
R=os.path.dirname(os.path.dirname(os.path.abspath(__file__))); A=os.path.join(R,"partitions_32m.csv"); S=os.path.join(R,"partitions_32m.system.csv"); F=32*1024*1024
def z(v):
 v=v.strip(); m=1
 if v[-1:] in "Kk":m,v=1024,v[:-1]
 elif v[-1:] in "Mm":m,v=1048576,v[:-1]
 return int(v,0)*m
def load(p):
 r=[]
 for x in csv.reader(open(p)):
  if not x or x[0].strip().startswith("#"):continue
  x=[q.strip() for q in x]+[""]*6;n,t,s,o,sz,fl=x[:6]
  if not o:sys.exit(f"{p}: {n} needs explicit offset")
  r.append(dict(name=n,type=t,subtype=s,offset=z(o),size=z(sz),flags=fl))
 return r
p=argparse.ArgumentParser();p.add_argument("--offset");a=p.parse_args();x,y=load(A),load(S)
if a.offset:
 for q in y:
  if q["name"]==a.offset:print(hex(q["offset"]));sys.exit()
 sys.exit("partition not found")
e=[]
if [q["name"] for q in x]!=[q["name"] for q in y]:e+=["partition names/order differ"]
for q,w in zip(x,y):
 for k in ("offset","size","flags"):
  if q[k]!=w[k]:e+=[f"{q['name']}: {k} differs"]
 if q["name"]=="recovery":
  if (q["type"],w["type"],w["subtype"])!=("data","app","factory"):e+=["recovery type contract failed"]
 elif (q["type"],q["subtype"])!=(w["type"],w["subtype"]):e+=[f"{q['name']}: type differs"]
end=0
for q in sorted(y,key=lambda q:q["offset"]):
 if q["offset"]<end:e+=[f"{q['name']} overlaps"]
 if q["type"]=="app" and q["offset"]%0x10000:e+=[f"{q['name']} not 64K aligned"]
 end=q["offset"]+q["size"]
if end>F:e+=["layout exceeds flash"]
if e:print("\n".join("error: "+q for q in e),file=sys.stderr);sys.exit(1)
print(f"partitions: OK (layout ends at {hex(end)}, {(F-end)//1024} KiB spare)")
