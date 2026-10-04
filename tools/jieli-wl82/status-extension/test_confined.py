"""Separate process: offline analysis and USB tools have different jltech trees."""
import hashlib
import unittest
import build_confined as build


class ConfinedTests(unittest.TestCase):
    def test_same_footprint_and_relocated_reviewed_body(self):
        original=build.loader.decode(build.loader.SOURCE.read_bytes())
        parts={n:(build.OUT/n).read_bytes() for _,_,n in build.REGIONS}
        combined=build.combine(original,parts)
        self.assertEqual(len(combined),len(original))
        self.assertEqual(hashlib.sha256(build.transport_encode(combined)).hexdigest(),build.ENCODED_SHA)
        old=(build.loader.ROOT/'tmp/wl82-status-extension/extension.bin').read_bytes()
        self.assertEqual(hashlib.sha256(old).hexdigest(),'a680e2c412a7d9e4346c45e5e76821d4cca380e56a73bd60a3cc930c32bccee1')
        self.assertEqual(old[22:],parts['extension.bin'][22:])
        self.assertEqual(parts['route.bin'],bytes.fromhex('c0 ea 51 00'))
        self.assertEqual(parts['hook.bin'],bytes.fromhex('c0 ea e4 02'))
        for name,data in parts.items():
            for index in range(len(data)):
                changed=dict(parts)
                mutation=bytearray(data);mutation[index]^=1;changed[name]=mutation
                with self.assertRaises(ValueError):build.combine(original,changed)


if __name__=='__main__':unittest.main()
