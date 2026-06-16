# -*- coding: utf-8 -*-
# Generates docs/factory-presets-erd.svg : authoritative, normalized ERD of
# factory/system presets from the slicer (resources/profiles) and the printer
# (K2 Plus /server/material). Keys & relations only; multi-valued fields and
# repeating groups normalized into junction/child tables.

HEADER_H, ROW_H, PAD = 26, 16, 8

class T:
    def __init__(self, tid, x, y, w, title, cls, rows):
        self.id, self.x, self.y, self.w, self.title, self.cls, self.rows = tid, x, y, w, title, cls, rows
        self.h = HEADER_H + PAD*2 + ROW_H*len(rows)
    def rowy(self, i):   # vertical center of row i
        return self.y + HEADER_H + PAD + i*ROW_H + ROW_H//2
    def L(self, i=None): return (self.x, self.rowy(i) if i is not None else self.y+self.h/2)
    def R(self, i=None): return (self.x+self.w, self.rowy(i) if i is not None else self.y+self.h/2)
    def Ttop(self, fx=0.5): return (self.x+self.w*fx, self.y)
    def B(self, fx=0.5):    return (self.x+self.w*fx, self.y+self.h)

# rows: (badge, text, ref)  badge in {'PK','FK','PK,FK','AK',''}
tables = [
 # ---- SLICER ----
 T('vendor',40,92,210,'vendor','ent',[('PK','name',''),('','version',''),('','force_update','')]),
 T('mm',620,92,275,'machine_model','ent',[('PK','name',''),('','model_id',''),('','family',''),('','machine_tech',''),('','default_bed_type','')]),
 T('machine',620,300,275,'machine','ent',[('PK','name',''),('FK','inherits','machine'),('FK','printer_model','machine_model'),('FK','default_print_profile','process'),('','type / from / instantiation',''),('','setting_id',''),('','printer_variant','')]),
 T('process',620,560,275,'process','ent',[('PK','name',''),('FK','inherits','process'),('','type / from / instantiation',''),('','setting_id',''),('','compatible_printers_condition','')]),
 T('filament',620,735,275,'filament','ent',[('PK','name',''),('FK','inherits','filament'),('FK','filament_id','material (slicer)'),('','type / from / instantiation',''),('','setting_id',''),('','filament_type',''),('','filament_vendor',''),('','compatible_printers_condition','')]),
 T('matS',620,978,275,'material (slicer)','mat',[('PK','filament_id',''),('','filament_type','')]),
 # ---- manifest junctions (COL1) ----
 T('vmml',300,92,250,'vendor_machine_model_list','jn',[('PK,FK','vendor','vendor'),('PK,FK','name','machine_model'),('','sub_path','')]),
 T('vml',300,300,250,'vendor_machine_list','jn',[('PK,FK','vendor','vendor'),('PK,FK','name','machine'),('','sub_path','')]),
 T('vpl',300,560,250,'vendor_process_list','jn',[('PK,FK','vendor','vendor'),('PK,FK','name','process'),('','sub_path','')]),
 T('vfl',300,735,250,'vendor_filament_list','jn',[('PK,FK','vendor','vendor'),('PK,FK','name','filament'),('','sub_path','')]),
 # ---- sub junctions (COL3) ----
 T('mmn',965,92,290,'machine_model_nozzle','jn',[('PK,FK','machine_model','machine_model'),('PK','nozzle_diameter','')]),
 T('mmdm',965,210,290,'machine_model_default_material','jn',[('PK,FK','machine_model','machine_model'),('PK','material_name','')]),
 T('mn',965,330,290,'machine_nozzle','jn',[('PK,FK','machine','machine'),('PK','nozzle_diameter','')]),
 T('mdf',965,448,290,'machine_default_filament','jn',[('PK,FK','machine','machine'),('PK,FK','name','filament')]),
 T('pcp',965,600,290,'process_compatible_printer','jn',[('PK,FK','process','process'),('PK,FK','name','machine')]),
 T('fcp',965,775,290,'filament_compatible_printer','jn',[('PK,FK','filament','filament'),('PK,FK','name','machine')]),
 # ---- PRINTER ----
 T('matP',1360,560,250,'material (printer)','pr',[('PK','id  (U####)',''),('AK','name',''),('','brand',''),('','type','')]),
 T('kv',1360,775,250,'material_kv_param','prl',[('PK,FK','material_id','material (printer)'),('PK','key',''),('','value','')]),
]
TB = {t.id: t for t in tables}

def esc(s): return s.replace('&','&amp;').replace('<','&lt;').replace('>','&gt;')

# ---------- routing ----------
def stub(p, d, s=18):
    x,y = p
    return {'L':(x-s,y),'R':(x+s,y),'T':(x,y-s),'B':(x,y+s)}[d]

def route(p1,d1,p2,d2):
    a1,a2 = stub(p1,d1), stub(p2,d2)
    h1 = d1 in 'LR'; h2 = d2 in 'LR'
    if h1 and h2:
        mx=(a1[0]+a2[0])/2; pts=[p1,a1,(mx,a1[1]),(mx,a2[1]),a2,p2]
    elif h1 and not h2:
        pts=[p1,a1,(a2[0],a1[1]),a2,p2]
    elif not h1 and h2:
        pts=[p1,a1,(a1[0],a2[1]),a2,p2]
    else:
        my=(a1[1]+a2[1])/2; pts=[p1,a1,(a1[0],my),(a2[0],my),a2,p2]
    return pts

def dpath(pts):
    return 'M'+' L'.join(f'{x:.1f},{y:.1f}' for x,y in pts)

def longest_seg_mid(pts):
    best=0; mid=pts[len(pts)//2]
    for i in range(len(pts)-1):
        (x1,y1),(x2,y2)=pts[i],pts[i+1]
        d=abs(x2-x1)+abs(y2-y1)
        if d>best: best=d; mid=((x1+x2)/2,(y1+y2)/2)
    return mid

svg=[]
# edges drawn first (under tables); labels collected for later
labels=[]
def edge(p_one, p_many, label='', dashed=False, color='#475569'):
    # p_one/p_many = (table_id, side, row)
    def anc(spec):
        tid,side,row=spec; t=TB[tid]
        return {'L':t.L,'R':t.R}.get(side, lambda r: (t.Ttop()[0],t.y) if side=='T' else (t.B()[0],t.y+t.h))(row)
    def pt(spec):
        tid,side,row=spec; t=TB[tid]
        if side=='L': return t.L(row)
        if side=='R': return t.R(row)
        if side=='T': return t.Ttop(row if row is not None else 0.5)
        if side=='B': return t.B(row if row is not None else 0.5)
    p1,d1=pt(p_one),p_one[1]
    p2,d2=pt(p_many),p_many[1]
    pts=route(p1,d1,p2,d2)
    da=' stroke-dasharray="6 4"' if dashed else ''
    svg.append(f'<path d="{dpath(pts)}" fill="none" stroke="{color}" stroke-width="1.4"{da} marker-start="url(#one)" marker-end="url(#many)"/>')
    if label:
        mx,my=longest_seg_mid(pts); labels.append((mx,my,label))

def selfloop(tid, label):
    t=TB[tid]
    x1,y1=t.Ttop(0.74); x2,y2=t.Ttop(0.9)
    svg.append(f'<path d="M{x1:.1f},{y1:.1f} C{x1:.1f},{y1-36:.1f} {x2:.1f},{y2-36:.1f} {x2:.1f},{y2:.1f}" fill="none" stroke="#475569" stroke-width="1.4" marker-start="url(#many)" marker-end="url(#one)"/>')
    labels.append(((x1+x2)/2,y1-30,label))

def cross(label):
    a=TB['filament'].R(6); b=TB['matP'].L(2)
    pts=[a,(a[0]+24,a[1]),(b[0]-24,b[1]),b]
    svg.append(f'<path d="{dpath(pts)}" fill="none" stroke="#0891b2" stroke-width="1.5" stroke-dasharray="2 4"/>')
    mx=(a[0]+b[0])/2; labels.append((mx,a[1]-7,label,'#0e7490'))

# associative: parent(one) -> junction(many)
edge(('vendor','R',None),('vmml','L',0))
edge(('vendor','R',None),('vml','L',0))
edge(('vendor','R',None),('vpl','L',0))
edge(('vendor','R',None),('vfl','L',0))
edge(('mm','L',0),('vmml','R',1))
edge(('machine','L',0),('vml','R',1))
edge(('process','L',0),('vpl','R',1))
edge(('filament','L',0),('vfl','R',1))
# machine_model children
edge(('mm','R',0),('mmn','L',0))
edge(('mm','R',0),('mmdm','L',0))
edge(('machine','R',0),('mn','L',0))
edge(('machine','R',0),('mdf','L',0))
edge(('process','R',0),('pcp','L',0))
edge(('filament','R',0),('fcp','L',0))
# entity FKs (child many -> parent one): bar at parent, crow at child
edge(('mm','B',0.4),('machine','T',0.45),'printer_model')
edge(('process','T',0.4),('machine','B',0.45),'default_print_profile')
edge(('matS','T',0.5),('filament','B',0.5),'filament_id')
edge(('filament','R',1),('mdf','L',1))                 # machine_default_filament.name -> filament
edge(('machine','R',6),('pcp','L',1))                  # process_compatible_printer.name -> machine
edge(('machine','R',5),('fcp','L',1))                  # filament_compatible_printer.name -> machine
edge(('matP','B',0.5),('kv','T',0.5))
edge(('filament','R',5),('mmdm','L',1),'soft · by series name',dashed=True,color='#94a3b8')
# self refs
selfloop('machine','inherits')
selfloop('process','inherits')
selfloop('filament','inherits')
# cross-system correspondence
cross('filament_vendor = brand  ·  name = name')

# ---------- render tables ----------
HCOL={'ent':'#2563eb','mat':'#7c3aed','jn':'#64748b','pr':'#16a34a','prl':'#4ade80'}
def render_table(t):
    out=[f'<rect x="{t.x}" y="{t.y}" width="{t.w}" height="{t.h}" rx="3" fill="#fff" stroke="#94a3b8" stroke-width="1.1"/>']
    out.append(f'<rect x="{t.x}" y="{t.y}" width="{t.w}" height="{HEADER_H}" rx="3" fill="{HCOL[t.cls]}"/>')
    out.append(f'<rect x="{t.x}" y="{t.y+HEADER_H-6}" width="{t.w}" height="6" fill="{HCOL[t.cls]}"/>')
    tc = '#0f172a' if t.cls=='prl' else '#fff'
    out.append(f'<text x="{t.x+10}" y="{t.y+18}" font-family="Segoe UI,sans-serif" font-size="12.5" font-weight="700" fill="{tc}">{esc(t.title)}</text>')
    for i,(badge,text,ref) in enumerate(t.rows):
        ry=t.rowy(i); ty=ry+4
        bx=t.x+8; tx=t.x+64
        if badge:
            bcol='#b45309' if 'PK' in badge else '#0d9488' if badge=='AK' else '#6d28d9'
            out.append(f'<text x="{bx}" y="{ty}" font-family="Consolas,monospace" font-size="9.5" font-weight="700" fill="{bcol}">{badge}</text>')
        deco=' text-decoration="underline"' if ('PK' in badge or badge=='AK') else ''
        fst=' font-style="italic"' if badge in ('FK','PK,FK') else ''
        out.append(f'<text x="{tx}" y="{ty}" font-family="Consolas,monospace" font-size="11" fill="#1e293b"{deco}{fst}>{esc(text)}</text>')
        if ref:
            out.append(f'<text x="{tx+len(text)*6.4+8}" y="{ty}" font-family="Consolas,monospace" font-size="9.5" fill="#64748b">▸ {esc(ref)}</text>')
    return ''.join(out)

W,H = 1640, 1100
doc=[f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Segoe UI,Roboto,sans-serif">']
doc.append('<defs>')
doc.append('<marker id="many" viewBox="0 0 16 12" refX="15" refY="6" markerWidth="16" markerHeight="12" orient="auto" markerUnits="userSpaceOnUse"><path d="M1,6 L15,0 M1,6 L15,6 M1,6 L15,12" fill="none" stroke="#475569" stroke-width="1.3"/></marker>')
doc.append('<marker id="one" viewBox="0 0 16 12" refX="15" refY="6" markerWidth="16" markerHeight="12" orient="auto" markerUnits="userSpaceOnUse"><path d="M9,0 L9,12" fill="none" stroke="#475569" stroke-width="1.3"/></marker>')
doc.append('</defs>')
doc.append(f'<rect x="0" y="0" width="{W}" height="{H}" fill="#f8fafc"/>')
doc.append(f'<text x="36" y="34" font-size="18" font-weight="700" fill="#0f172a">Factory-original presets — slicer ⋈ printer : normalized ERD (keys &amp; relations)</text>')
# legend
leg=[('#2563eb','entity'),('#64748b','junction / associative'),('#7c3aed','material (slicer id-space)'),('#16a34a','printer'),]
lx=36
doc.append('<g font-size="11" fill="#475569">')
for c,t in leg:
    doc.append(f'<rect x="{lx}" y="46" width="13" height="13" rx="2" fill="{c}"/><text x="{lx+18}" y="56">{t}</text>')
    lx += 30 + len(t)*6.6
doc.append(f'<text x="{lx+6}" y="56">PK underline · FK italic ▸ ref · crow’s-foot = many / bar = one · dashed = non-key correspondence</text>')
doc.append('</g>')
# divider
doc.append(f'<line x1="1320" y1="66" x2="1320" y2="{H-20}" stroke="#cbd5e1" stroke-width="1.3" stroke-dasharray="4 4"/>')
doc.append('<text x="640" y="80" font-size="12" font-weight="700" fill="#1d4ed8">SLICER  ·  resources/profiles/&lt;Vendor&gt;.json</text>')
doc.append('<text x="1360" y="80" font-size="12" font-weight="700" fill="#15803d">PRINTER  ·  K2 Plus /server/material</text>')

doc.extend(svg)                       # edges under
for t in tables: doc.append(render_table(t))   # tables over
for lb in labels:                     # labels on top
    x,y,txt = lb[0],lb[1],lb[2]; col = lb[3] if len(lb)>3 else '#334155'
    w=len(txt)*5.9+8
    doc.append(f'<rect x="{x-w/2:.1f}" y="{y-9:.1f}" width="{w:.1f}" height="14" rx="2" fill="#f8fafc" opacity="0.92"/>')
    doc.append(f'<text x="{x:.1f}" y="{y+2:.1f}" font-size="10" font-weight="600" fill="{col}" text-anchor="middle">{esc(txt)}</text>')
doc.append('</svg>')

open(r'I:\IdeaProjects\CrealityPrint\docs\factory-presets-erd.svg','w',encoding='utf-8').write('\n'.join(doc))
print('wrote docs/factory-presets-erd.svg')
