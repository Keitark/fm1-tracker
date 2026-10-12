"""Exercise creation and configuration-to-real-builder translation without devices."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from config import defaults,validate,HALS
from create_project import create
from build_project import command
class Projects(unittest.TestCase):
    def test_new_only_and_safe_names(self):
        with tempfile.TemporaryDirectory() as t:
            p=create(t,'my-synth','My Synth');source=(p/'user.c').read_bytes()
            with self.assertRaises(FileExistsError):create(t,'my-synth')
            self.assertEqual(source,(p/'user.c').read_bytes())
            for name in ('../escape','con','com1','x/y','Upper',''):
                with self.assertRaises(ValueError):create(t,name)
    def test_config_reaches_builder(self):
        with tempfile.TemporaryDirectory() as t:
            p=create(t,'starter');c=defaults('starter','Starter');c.update(module='user',clockPreset='sdk-320',lcdPreset='stock-dma');c['usb']['uac']=False
            (p/'forge.json').write_text(json.dumps(c));args=command(p,p/'output','test-python')
            self.assertEqual(args[0],'test-python');self.assertIn('--factory-diag',args);self.assertIn('--forge-no-uac',args)
            self.assertEqual(args[args.index('--forge-clock')+1],'sdk-320');self.assertEqual(args[args.index('--forge-lcd')+1],'stock-dma')
            self.assertEqual(Path(args[args.index('--forge-user-module')+1]),p/'user.c')
            (p/'user.c').unlink()
            with self.assertRaises(ValueError):command(p,p/'output')
    def test_clock_lcd_compatibility_before_builder(self):
        for lcd in ('spi15-rgb444','spi30-rgb444'):
            with tempfile.TemporaryDirectory() as t:
                good=defaults('starter','Starter');good['lcdPreset']=lcd
                p=create(t,'starter',configuration=good)
                args=command(p,p/'output','test-python')
                self.assertEqual(args[args.index('--forge-lcd')+1],lcd)
                bad=copy.deepcopy(good);bad['clockPreset']='sdk-320'
                with self.assertRaisesRegex(ValueError,'sdk-320 requires stock-dma'):validate(bad)
                (p/'forge.json').write_text(json.dumps(bad))
                with self.assertRaisesRegex(ValueError,'sdk-320 requires stock-dma'):command(p,p/'output')
    def test_contract_rejects_unknown_or_unfixed_usb(self):
        base=defaults('starter','Starter')
        for field,value in (('clockPreset','sdk-396'),('module','doom'),('schemaVersion',3),('schemaVersion',True),('goal','unversioned')):
            c=copy.deepcopy(base);c[field]=value
            with self.assertRaises(ValueError):validate(c)
        for field in ('cdc','uboot'):
            c=copy.deepcopy(base);c['usb'][field]=False
            with self.assertRaises(ValueError):validate(c)
    def test_plain_module_default_keeps_uac(self):
        with tempfile.TemporaryDirectory() as t:
            p=create(t,'diagnostic');args=command(p,p/'out')
            self.assertNotIn('--forge-user-module',args);self.assertNotIn('--forge-no-uac',args)
            self.assertEqual(json.loads((p/'forge.json').read_text())['schemaVersion'],2)
            for name in HALS:self.assertEqual(args[args.index('--forge-hal-'+name)+1],'1')
    def test_legacy_is_strict_and_normalized_without_mutation(self):
        base=defaults('starter','Starter');base.pop('hal');base['schemaVersion']=1
        original=copy.deepcopy(base);normalized=validate(base)
        self.assertEqual(base,original);self.assertEqual(normalized['schemaVersion'],2)
        self.assertEqual(normalized['hal'],dict.fromkeys(HALS,True))
        normalized['usb']['uac']=False;self.assertTrue(base['usb']['uac'])
        base['hal']=dict.fromkeys(HALS,True)
        with self.assertRaises(ValueError):validate(base)
    def test_each_hal_selection_reaches_builder_with_uac_independent(self):
        with tempfile.TemporaryDirectory() as t:
            p=create(t,'starter');base=defaults('starter','Starter');base['module']='user'
            for disabled in HALS:
                c=copy.deepcopy(base);c['hal'][disabled]=False
                (p/'forge.json').write_text(json.dumps(c));args=command(p,p/'out')
                self.assertNotIn('--forge-no-uac',args)
                flags=[args[args.index('--forge-hal-'+name)+1] for name in HALS]
                self.assertEqual(flags,['0' if name==disabled else '1' for name in HALS])
    def test_hal_fields_require_exact_booleans_and_no_extras(self):
        base=defaults('starter','Starter')
        mutations=[None,{},dict(base['hal'],gpio=True),dict(base['hal'],audio=1),dict(base['hal'],lcd='false')]
        for value in mutations:
            c=copy.deepcopy(base);c['hal']=value
            with self.subTest(value=value),self.assertRaises(ValueError):validate(c)
        base.pop('hal')
        with self.assertRaises(ValueError):validate(base)
if __name__=='__main__':unittest.main()
