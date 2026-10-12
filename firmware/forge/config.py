"""Versioned Forge project validation. No devices, SDK execution or imports."""
import re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
CLOCKS=('sdk-default','sdk-320')
LCDS=('stock-dma','spi15-rgb444','spi30-rgb444')
HALS=('buttons','encoders','volume','lcd','audio')
RESERVED={'con','prn','aux','nul'}|{f'{p}{i}' for p in ('com','lpt') for i in range(1,10)}
def slug(value):
    if not isinstance(value,str) or not re.fullmatch(r'[a-z][a-z0-9-]{0,47}',value) or value in RESERVED:
        raise ValueError('Project slug must be1-48 lower-case letters/digits/hyphens, start with a letter, and not a Windows reserved name')
    return value
def validate(value):
    required={'schemaVersion','project','profile','clockPreset','lcdPreset','module','usb'}
    if not isinstance(value,dict) or type(value.get('schemaVersion')) is not int or value['schemaVersion'] not in (1,2):raise ValueError('Unsupported Forge schemaVersion')
    version=value['schemaVersion']
    if version==2:required.add('hal')
    if set(value)!=required:raise ValueError('Forge project fields must match its schemaVersion')
    p=value['project']
    if not isinstance(p,dict) or set(p)!={'slug','displayName'}:raise ValueError('Invalid project metadata')
    slug(p['slug'])
    name=p['displayName']
    if not isinstance(name,str) or not name.strip() or len(name)>64 or any(ord(c)<32 for c in name):raise ValueError('displayName must contain1-64 printable characters')
    if value['profile']!='forge-diag' or value['clockPreset'] not in CLOCKS or value['lcdPreset'] not in LCDS or value['module'] not in ('diagnostics','user'):raise ValueError('Unsupported Forge profile/module/preset')
    if value['clockPreset']=='sdk-320' and value['lcdPreset']!='stock-dma':raise ValueError('sdk-320 requires stock-dma: its53MHz LSB clock cannot provide the60MHz required by fast LCD presets')
    usb=value['usb']
    if not isinstance(usb,dict) or set(usb)!={'cdc','uac','uboot'} or usb['cdc'] is not True or usb['uboot'] is not True or type(usb['uac']) is not bool:raise ValueError('USB CDC and UBOOT are fixed; uac must be a boolean')
    hal=value['hal'] if version==2 else dict.fromkeys(HALS,True)
    if not isinstance(hal,dict) or set(hal)!=set(HALS) or any(type(hal[k]) is not bool for k in HALS):raise ValueError('hal must contain exactly five boolean user-access selections')
    # Normalize legacy projects without mutating caller-owned input or USB data.
    return dict(value,schemaVersion=2,project=dict(p),usb=dict(usb),hal=dict(hal))
def defaults(project_slug,display_name):
    return validate({'schemaVersion':2,'project':{'slug':project_slug,'displayName':display_name},'profile':'forge-diag','clockPreset':'sdk-default','lcdPreset':'stock-dma','module':'diagnostics','usb':{'cdc':True,'uac':True,'uboot':True},'hal':dict.fromkeys(HALS,True)})
