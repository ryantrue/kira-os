#!/usr/bin/env python3
import argparse,csv,os,sys
ROOT=os.path.dirname(os.path.dirname(os.path.abspath(__file__))); APP_VIEW=os.path.join(ROOT,"partitions_32m.csv"); SYSTEM=os.path.join(ROOT,"partitions_32m.system.csv"); FLASH_SIZE=32*1024*1024
def parse_size(v):
 v=v.strip(); m=1
 if v[-1:] in "Kk":m,v=1024,v[:-1]
 elif v[-1:] in "Mm":m,v=1024*1024,v[:-1]
 return int(v,0)*m
def load(path):
 rows=[]
 with open(path,newline="") as f:
  for raw in csv.reader(f):
   if not raw or raw[0].strip().startswith("#"):continue
   c=[x.strip() for x in raw]+[""]*6; name,t,st,off,size,flags=c[:6]
   if not off:sys.exit(f"{path}: `{name}` needs an explicit offset")
   rows.append(dict(name=name,type=t,subtype=st,offset=parse_size(off),size=parse_size(size),flags=flags))
 return rows
def main():
 p=argparse.ArgumentParser();p.add_argument("--offset",metavar="NAME");a=p.parse_args(); av,sy=load(APP_VIEW),load(SYSTEM)
 if a.offset:
  for r in sy:
   if r["name"]==a.offset:print(hex(r["offset"]));return
  sys.exit(f"no partition named {a.offset}")
 e=[]
 if [r["name"] for r in av]!=[r["name"] for r in sy]:e.append("partition names or order differ")
 for x,y in zip(av,sy):
  for k in ("offset","size","flags"):
   if x[k]!=y[k]:e.append(f"{x['name']}: {k} differs")
  if x["name"]=="recovery":
   if (x["type"],y["type"],y["subtype"])!=("data","app","factory"):e.append("recovery must be data in app view and app/factory in system table")
  elif (x["type"],x["subtype"])!=(y["type"],y["subtype"]):e.append(f"{x['name']}: type/subtype differ")
 end=0
 for r in sorted(sy,key=lambda r:r["offset"]):
  if r["offset"]<end:e.append(f"{r['name']} overlaps previous partition")
  if r["type"]=="app" and r["offset"]%0x10000:e.append(f"{r['name']}: app partitions must be 64K aligned")
  end=r["offset"]+r["size"]
 if end>FLASH_SIZE:e.append("layout exceeds 32 MiB")
 if e:
  print("\n".join("error: "+x for x in e),file=sys.stderr);sys.exit(1)
 print(f"partitions: OK (layout ends at {hex(end)}, {(FLASH_SIZE-end)//1024} KiB spare)")
if __name__=="__main__":main()
