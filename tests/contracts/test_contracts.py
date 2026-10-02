"""The public ABI and wire contract must match the production implementation."""
import json
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]

class ContractTests(unittest.TestCase):
    def test_authoritative_contract_is_present(self):
        self.assertTrue((ROOT / "contracts/abi-v1.json").is_file(), "core must own ABI v1 contract")

    def test_native_signatures_permissions_and_exports(self):
        path = ROOT / "contracts/abi-v1.json"
        self.assertTrue(path.is_file(), "core must own ABI v1 contract")
        spec = json.loads(path.read_text(encoding="utf-8"))
        source = (ROOT / "runtime/wasm/tinyrt_runtime.c").read_text(encoding="utf-8")
        natives = re.findall(r'\{ "(\w+)", \(void \*\)\w+, "([()i]+)", NULL \}', source)
        self.assertEqual(natives, [(x["name"], x["signature"]) for x in spec["imports"]])
        permissions = re.search(r'required_permission\[\] = \{([^}]+)', source).group(1)
        self.assertEqual([int(x.strip()) for x in permissions.split(",")], [x["permission"] for x in spec["imports"]])
        native_arity = re.search(r'arity\[\] = \{([^}]+)', source).group(1)
        self.assertEqual([int(x.strip()) for x in native_arity.split(",")], [x["signature"].split(")")[0].count("i") for x in spec["imports"]])
        exports = re.search(r'exports\[\] = \{([^}]+)', source).group(1)
        self.assertEqual(re.findall(r'"(\w+)"', exports), [x["name"] for x in spec["exports"]])
        export_arity = re.search(r'export_arity\[\] = \{([^}]+)', source).group(1)
        self.assertEqual([int(x.strip()) for x in export_arity.split(",")], [x["arity"] for x in spec["exports"]])
        header = (ROOT / "contracts/guest-v1.h").read_text(encoding="utf-8")
        self.assertEqual(re.findall(r'TINYRT_IMPORT\((\w+)\) (?:int32_t|uint32_t)', header), [x["name"] for x in spec["imports"]])
        prototypes = re.findall(r'TINYRT_IMPORT\((\w+)\) (?:int32_t|uint32_t) \w+\(([^;]*)\);', header)
        for (name, args), expected in zip(prototypes, spec["imports"]):
            arity = 0 if args.strip() == "void" else args.count(",") + 1
            self.assertEqual(arity, expected["signature"].split(")")[0].count("i"), name)
        for name, value in spec["events"].items():
            self.assertRegex(header, r"#define " + name + " " + str(value) + r"\b")
        self.assertEqual([x["name"] for x in spec["exports"] if x.get("optional")], ["tinyrt_stop"])
        for item in spec["exports"]:
            declaration = re.search(r'int32_t ' + item["name"] + r'\(([^;]*)\);', header)
            self.assertIsNotNone(declaration, item["name"])
            args = declaration.group(1)
            self.assertEqual(0 if args.strip() == "void" else args.count(",") + 1, item["arity"])

    def test_wire_constants_match_c_headers(self):
        path = ROOT / "contracts/wire-v1.json"
        self.assertTrue(path.is_file(), "core must own package/protocol contract")
        spec = json.loads(path.read_text(encoding="utf-8"))
        for relative, values in spec["headers"].items():
            source = (ROOT / relative).read_text(encoding="utf-8")
            for name, expected in values.items():
                found = re.search(r"#define " + name + r" (?:UINT32_C\()?((?:0x)?[0-9A-Fa-f]+)[uU]?", source)
                self.assertIsNotNone(found, name)
                self.assertEqual(int(found.group(1), 0), expected, name)

    def test_paging_and_extent_contract_bounds(self):
        spec = json.loads((ROOT / "contracts/wire-v1.json").read_text(encoding="utf-8"))
        store = spec["store"]
        self.assertEqual((store["magic"], store["format_version"], store["record_size"],
                          store["record_package_offset"]), ("TRDIR001", 1, 80, 36))
        limits = spec["headers"]["runtime/include/tinyrt_store.h"]
        self.assertEqual(limits["TINYRT_STORE_SIZE"], 0x4e0000)
        self.assertEqual(limits["TINYRT_STORE_MAX_PACKAGE_SIZE"], 0x200000)
        self.assertEqual(limits["TINYRT_STORE_MAX_APPS"], 16)
        self.assertLessEqual(32 + 16 * store["record_size"], 4088)
        page = spec["management"]["paged_list"]
        self.assertEqual(page["request_size"], 10)
        self.assertEqual(page["request"], {
            "kind": {"offset": 0, "type": "u8", "values": {"healthy": 0, "quarantined": 1}},
            "offset": {"offset": 1, "type": "u8"},
            "generation": {"offset": 2, "type": "u64le"},
        })
        self.assertEqual(page["response_header_size"], 11)
        self.assertEqual(page["response"], {
            "generation": {"offset": 0, "type": "u64le"},
            "total": {"offset": 8, "type": "u8"},
            "offset": {"offset": 9, "type": "u8"},
            "count": {"offset": 10, "type": "u8"},
            "records": {"offset": 11, "record_size": 72, "max_count": 3},
        })
        self.assertLessEqual(10 + 11 + 3 * 72, spec["management"]["max_message"])
        self.assertGreater(10 + 11 + 4 * 72, spec["management"]["max_message"])
        self.assertEqual(spec["management"]["legacy_list"]["max_records"], 2)

    def test_storage_and_metadata_layouts(self):
        spec = json.loads((ROOT / "contracts/wire-v1.json").read_text(encoding="utf-8"))
        storage=spec["management"]["storage"]
        self.assertEqual((storage["opcode"],storage["request_size"],storage["response_size"],storage["schema_version"]),(25,0,92,1))
        self.assertEqual(storage["flags"],{"ram_valid":1})
        self.assertEqual(storage["reserved_value"],0)
        fields=storage["response"]
        expected=(('schema',0,2),('flags',2,2),('generation',4,8),('package_total_bytes',12,4),
          ('data_bytes',16,4),('package_bytes',20,4),('allocated_bytes',24,4),('free_bytes',28,4),
          ('largest_free_bytes',32,4),('max_package_size',36,4),('max_apps',40,2),('installed_count',42,2),
          ('quarantined_count',44,2),('reserved',46,2),('app_data_total_bytes',48,4),
          ('internal_ram_total_bytes',52,4),('internal_ram_free_bytes',56,4),('internal_ram_largest_free_bytes',60,4),
          ('external_ram_total_bytes',64,4),('external_ram_free_bytes',68,4),('external_ram_largest_free_bytes',72,4),
          ('runtime_heap_limit',76,4),('runtime_heap_used',80,4),('runtime_heap_peak',84,4),('shared_assets_total_bytes',88,4))
        self.assertEqual(set(fields),{name for name,_,_ in expected})
        for name,offset,size in expected:
            self.assertEqual(fields[name],{'offset':offset,'type':f'u{size*8}le'})
        info=spec['management']['app_info']
        self.assertEqual((info['opcode'],info['request_size'],info['response_size']),(26,70,248))
        self.assertEqual(info['response']['app'],{'offset':8,'type':'info72'})
        self.assertEqual(info['response']['title'],{'offset':80,'type':'utf8[64]'})
        for i,name in enumerate(('key_id','abi_version','permissions','memory_pages','instruction_budget','wasm_size','assets_size')):
            self.assertEqual(info['response'][name],{'offset':144+4*i,'type':'u32le'})
        self.assertLessEqual(2+info['response_size'],256)

if __name__ == "__main__":
    unittest.main()
